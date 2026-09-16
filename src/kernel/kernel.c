#include "kernel.h"

#include "panic.h"
#include "assert.h"

#include "common/error.h"
#include "fs/fs.h"
#include "hal/hal.h"
#include "vfs/vfs.h"

#include "common/stdio.h"
#include "arch/arch.h"

#include "drivers/disk/ata.h"
#include "drivers/disk/partition.h"
#include "drivers/keyboard/keyboard.h"

#include "console/console.h"
#include "kernel/device/device.h"
#include "drivers/video/fb/fb.h"
#include "common/string.h"
#include "klog/klog.h"
#include "memory/kernel_layout.h"
#include "memory/pmm.h"
#include "memory/pmm_layout.h"
#include "memory/region.h"
#include "memory/reservations.h"
#include "memory/heap.h"
#include "arch/x86/cpu/paging.h"
#include "syscall/syscall.h"
#include "process/process.h"
#include "scheduler/scheduler.h"
#include "usermode/usermode.h"

void cstart(boot_info_t *boot)
{
    if (!boot)
    {
        kernel_panic("missing boot_info_t");
    }

    kernel_main(boot);
}

void kernel_main(boot_info_t *boot)
{
    ata_device_t ata;
    partition_device_t part;
    fs_mount_t mount;

    // bring up serial logging first: it needs no interrupts and works before
    // the VGA console, so boot diagnostics are visible over QEMU -serial even
    // if later init faults.
    klog_init();
    KLOG_INFO("kernel: entered 32-bit C main");

    // set up the device registry before any driver init tries to register.
    device_registry_init();

    hal_initialize();

    if (kernel_end_address() <= kernel_start_address())
    {
        kernel_panic("invalid kernel layout");
    }

    memory_reserve_kernel();
    memory_reserve_stage2();

    pmm_layout_init(boot);
    memory_reserve_pmm_bitmap(pmm_bitmap_start(), pmm_bitmap_end());

    pmm_init(boot);

    heap_init();

    paging_init();

    // bring up the linear framebuffer (if the bootloader set a graphics mode).
    // must follow paging_init: the LFB lives above identity-mapped RAM.
    fb_init(boot);

    // bring up the console now that the framebuffer (if any) is mapped. it
    // picks the framebuffer backend when present, else the VGA text buffer.
    // serial klog has covered diagnostics up to this point.
    console_initialize(boot);
    stdio_set_putchar(console_putchar);
    printf("Kernel: entered 32-bit C main\r\n");

    syscall_initialize();

    process_init();

    scheduler_init();
    KLOG_INFO("kernel: subsystems up (pmm, heap, paging, syscalls, scheduler)");

    // enable hardware interrupts (STI).
    // hal_initialize() has fully installed all exception handlers (vectors 0–31),
    // IRQ handlers (vectors 32–47), and device driver handlers. Every gate that
    // can fire is now present and backed by a registered handler. No interrupt
    // can arrive before this point because the CPU holds IF=0 from boot.
    arch_enable_interrupts();

    if (!ata_initialize(&ata, ATA_CHANNEL_PRIMARY, ATA_DRIVE_MASTER))
    {
        kernel_panic("ATA initialization failed");
    }

    // register the disk with the device model. ata/part live for the lifetime
    // of the kernel (kernel_main never returns), so pointing at them is safe.
    static device_t disk_device;
    strncpy(disk_device.name, "hda", DEVICE_NAME_MAX - 1);
    disk_device.name[DEVICE_NAME_MAX - 1] = '\0';
    disk_device.type = DEVICE_TYPE_BLOCK;
    disk_device.ops = NULL;
    disk_device.driver_data = &ata.block;
    device_register(&disk_device);

    if (!partition_detect_ext2(&part, &ata.block))
    {
        kernel_panic("EXT2 partition detection failed");
    }

    static device_t part_device;
    strncpy(part_device.name, "hda1", DEVICE_NAME_MAX - 1);
    part_device.name[DEVICE_NAME_MAX - 1] = '\0';
    part_device.type = DEVICE_TYPE_BLOCK;
    part_device.ops = NULL;
    part_device.driver_data = &part.block;
    device_register(&part_device);

    if (!fs_mount(&mount, &part.block))
    {
        kernel_panic("EXT2 mount failed");
    }

    if (kerr_failed(vfs_mount_root(&mount)))
    {
        kernel_panic("VFS root mount failed");
    }

    KLOG_INFO("kernel: root filesystem mounted, launching /bin/sh");

    // launch the userspace shell as the first process
    const char *argv[] = {"sh", NULL};
    int ret = exec_program("/bin/sh", argv);
    if (ret < 0)
    {
        kernel_panic("failed to launch /bin/sh");
    }

    // shell exited. halt
    KLOG_WARN("kernel: shell exited with code %d, system halted", ret);
    printf("Shell exited with code %d. System halted.\r\n", ret);
    for (;;)
        ;
}

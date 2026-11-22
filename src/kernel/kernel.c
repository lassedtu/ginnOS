#include "kernel.h"

#include "panic.h"
#include "assert.h"

#include "common/error.h"
#include "fs/fs.h"
#include "fs/devfs.h"
#include "fs/tmpfs.h"
#include "fs/procfs.h"
#include "fs/block_cache.h"
#include "console/termios.h"
#include "time/clock.h"
#include "hal/hal.h"
#include "vfs/vfs.h"

#include "common/stdio.h"
#include "arch/arch.h"

#include "drivers/disk/ata.h"
#include "drivers/disk/partition.h"
#include "drivers/disk/ramdisk.h"
#include "drivers/disk/ahci.h"
#include "drivers/disk/nvme.h"
#include "drivers/pci/pci.h"
#include "drivers/mouse/ps2_mouse.h"
#include "drivers/sound/ac97.h"
#include "drivers/sound/hda.h"
#include "drivers/usb/xhci.h"
#include "acpi/acpi.h"
#include "arch/x86/cpu/apic.h"
#include "time/timer.h"
#include "drivers/keyboard/keyboard.h"

#include "console/console.h"
#include "kernel/device/device.h"
#include "kernel/input/input.h"
#include "kernel/tty/pty.h"
#include "kernel/device/null.h"
#include "drivers/video/display.h"
#include "drivers/video/fb/fb.h"
#include "drivers/video/virtio_gpu.h"
#include "common/memory.h"
#include "common/string.h"
#include "klog/klog.h"
#include "memory/kernel_layout.h"
#include "arch/x86/boot/multiboot2.h"
#include "memory/pmm.h"
#include "memory/pmm_layout.h"
#include "memory/region.h"
#include "memory/reservations.h"
#include "memory/heap.h"
#include "memory/kstack.h"
#include "memory/mmio.h"
#include "arch/x86/cpu/paging.h"
#include "syscall/syscall.h"
#include "process/process.h"
#include "scheduler/scheduler.h"
#include "usermode/usermode.h"

#ifdef KERNEL_TEST
#include "test/ktest.h"
#endif

#ifdef __x86_64__
#define ARCH_NAME "x86_64"
#else
#define ARCH_NAME "i686"
#endif

#ifndef __x86_64__

// the kernel's copy of the boot information. the bootloader's copy is in
// memory that the PMM gives away later.
static boot_info_t boot_info;

void cstart(phys_addr_t boot_phys, uint32_t magic)
{
    if (!boot_phys)
    {
        kernel_panic("missing boot information");
    }

    if (magic == MULTIBOOT2_BOOT_MAGIC)
    {
        if (!multiboot2_read(boot_phys, &boot_info))
        {
            kernel_panic("multiboot 2 information is not usable");
        }
    }
    else
    {
        // stage2 passes the physical address of its boot_info_t. it fills
        // in only the first part; the rest of boot_info stays zero.
        memcpy(&boot_info, phys_to_virt(boot_phys), BOOT_INFO_STAGE2_SIZE);
        boot_info.boot_protocol = BOOT_PROTOCOL_STAGE2;
    }

    kernel_main(&boot_info);
}

#endif // !__x86_64__

/**
 * register a block device with the device model. the device must exist
 * for the life of the kernel.
 */
static void register_block_device(device_t *device, const char *name, block_device_t *block)
{
    strncpy(device->name, name, DEVICE_NAME_MAX - 1);
    device->name[DEVICE_NAME_MAX - 1] = '\0';
    device->type = DEVICE_TYPE_BLOCK;
    device->ops = NULL;
    device->driver_data = block;
    device_register(device);
}

/**
 * use a partition as the root: read it through a sector cache (the cache
 * is optional; without memory for it the disk is read directly).
 */
static block_device_t *use_disk_root(partition_device_t *part, const char *disk_name)
{
    static block_cache_t root_cache;

    KLOG_INFO("kernel: root filesystem on %s", disk_name);
    if (kerr_failed(block_cache_init(&root_cache, &part->block)))
    {
        KLOG_WARN("kernel: no memory for the block cache; reading the disk directly");
        return &part->block;
    }
    block_cache_root = &root_cache;
    return &root_cache.block;
}

/**
 * find the block device with the root filesystem:
 *   1. a partition with the ginnOS root label on the primary ATA disk,
 *   2. or on a SATA disk of the AHCI controller, or on the NVMe disk (MBR
 *      or GPT),
 *   3. else a root image that the bootloader loaded as a module (a boot
 *      from a USB stick: the kernel has no USB storage driver). the changes
 *      are lost at the next boot.
 * the kernel only reads the partition tables and superblocks of the other
 * disks and partitions: it never writes to a disk of another operating
 * system on real hardware.
 * @return the root device, or NULL if there is none.
 */
static block_device_t *find_root_device(const boot_info_t *boot)
{
    // all static: the devices exist for the life of the kernel
    static ata_device_t ata;
    static partition_device_t part;
    static ramdisk_t ramdisk;
    static device_t disk_device;
    static device_t part_device;
    static device_t sata_devices[AHCI_MAX_DISKS];
    static device_t ram_device;
    static const char *const sata_names[AHCI_MAX_DISKS] = {"sda", "sdb", "sdc", "sdd"};

    if (ata_initialize(&ata, ATA_CHANNEL_PRIMARY, ATA_DRIVE_MASTER))
    {
        register_block_device(&disk_device, "hda", &ata.block);
        if (partition_find_ginnos_root(&part, &ata.block))
        {
            register_block_device(&part_device, "hda1", &part.block);
            return use_disk_root(&part, "the ATA disk");
        }
        KLOG_WARN("kernel: the ATA disk has no ginnOS root partition; not used");
    }

    uint32_t sata_count = ahci_init();
    for (uint32_t i = 0; i < sata_count; i++)
    {
        ahci_disk_t *disk = ahci_disk(i);
        register_block_device(&sata_devices[i], sata_names[i], &disk->block);
        if (partition_find_ginnos_root(&part, &disk->block))
        {
            register_block_device(&part_device, "root", &part.block);
            return use_disk_root(&part, sata_names[i]);
        }
    }

    static device_t nvme_device;
    nvme_disk_t *nvme = nvme_init();
    if (nvme)
    {
        register_block_device(&nvme_device, "nvme0", &nvme->block);
        if (partition_find_ginnos_root(&part, &nvme->block))
        {
            register_block_device(&part_device, "root", &part.block);
            return use_disk_root(&part, "nvme0");
        }
    }

    // the image must be in the direct map (always true on x86_64)
    phys_addr_t initrd_end = boot->initrd_end;
    if (boot->initrd_end > boot->initrd_start && initrd_end - 1 < PHYS_MAP_SIZE &&
        ramdisk_initialize(&ramdisk, phys_to_virt(boot->initrd_start),
                           boot->initrd_end - boot->initrd_start))
    {
        register_block_device(&ram_device, "ram0", &ramdisk.block);
        KLOG_INFO("kernel: root filesystem in RAM (a boot module); changes are lost at reboot");
        printf("Kernel: the root filesystem is in RAM. Changes are lost at reboot.\r\n");
        return &ramdisk.block;
    }

    return NULL;
}

void kernel_main(boot_info_t *boot)
{
    fs_mount_t mount;

    // bring up serial logging first: it needs no interrupts and works before
    // the VGA console, so boot diagnostics are visible over QEMU -serial even
    // if later init faults.
    klog_init();
    KLOG_INFO("kernel: entered C main (%s)", ARCH_NAME);

    // set up the device registry before any driver init tries to register.
    device_registry_init();
    null_devices_init();
    input_init();
    pty_init();

    hal_initialize();

    if (kernel_end_address() <= kernel_start_address())
    {
        kernel_panic("invalid kernel layout");
    }

    memory_reserve_kernel();
    memory_reserve_stage2();
    memory_reserve_initrd(boot->initrd_start, boot->initrd_end);

    pmm_layout_init(boot);
    memory_reserve_pmm_bitmap(pmm_bitmap_start(), pmm_bitmap_end());

    pmm_init(boot);

    heap_init();

    paging_init();

    // reserve the kernel stack region before any address space is cloned,
    // so its page table is shared by every process.
    kstack_init();

    // likewise the MMIO window (the framebuffer is mapped into it next)
    mmio_init();

    // the PCI devices, for the drivers of controllers that are not legacy
    // ISA devices (AHCI, NVMe)
    pci_init();

    // the ACPI tables: the interrupt controllers, the reset register
    acpi_init(boot);

    // the APICs instead of the 8259 PICs, if the MADT describes them (with
    // interrupts still off; the 8259 setup in hal_initialize stays as the
    // fallback)
    apic_init(TIMER_HZ);

    // a PS/2 mouse, if there is one
    ps2_mouse_init();

    // USB keyboards (many machines have no PS/2 keyboard)
    xhci_init();

    // sound output (/dev/dsp, /dev/mixer): Intel HDA, else AC'97
    if (!hda_init())
    {
        ac97_init();
    }

    // bring up the linear framebuffer (if the bootloader set a graphics mode).
    // must follow paging_init: the LFB lives above identity-mapped RAM.
    fb_init(boot);

    // a display driver that can change the mode takes over the screen,
    // at the size the screen wants, before the console sizes its text grid
    display_init();
    virtio_gpu_init();

    // bring up the console now that the framebuffer (if any) is mapped. it
    // picks the framebuffer backend when present, else the VGA text buffer.
    // serial klog has covered diagnostics up to this point.
    console_initialize(boot);
    stdio_set_putchar(console_putchar);
    printf("Kernel: entered C main (%s)\r\n", ARCH_NAME);

    syscall_initialize();

    // the FPU and SSE for user programs (their registers are saved on
    // every process switch); before the first process gets its clean state
    arch_fpu_init();

    process_init();

    scheduler_init();
    KLOG_INFO("kernel: subsystems up (pmm, heap, paging, syscalls, scheduler)");

    // the wall-clock time from the CMOS clock; ext2 stamps inodes with it
    clock_init();
    ext2_clock = clock_realtime_seconds;

    // enable hardware interrupts (STI).
    // hal_initialize() has fully installed all exception handlers (vectors 0–31),
    // IRQ handlers (vectors 32–47), and device driver handlers. Every gate that
    // can fire is now present and backed by a registered handler. No interrupt
    // can arrive before this point because the CPU holds IF=0 from boot.
    arch_enable_interrupts();

    block_device_t *root_device = find_root_device(boot);
    if (!root_device)
    {
        kernel_panic("no root filesystem: no ginnOS disk and no root image");
    }

    if (!fs_mount(&mount, root_device))
    {
        kernel_panic("EXT2 mount failed");
    }

    if (kerr_failed(vfs_mount_root(&mount)))
    {
        kernel_panic("VFS root mount failed");
    }

    // mount devfs at /dev so registered devices (fb0, tty0, hda, ...) appear as
    // nodes. static storage: vfs_mount keeps the pointer for the system's life.
    static fs_mount_t devfs;
    if (kerr_failed(devfs_mount(&devfs)) || kerr_failed(vfs_mount("/dev", &devfs)))
    {
        KLOG_WARN("kernel: devfs mount failed; /dev unavailable");
    }

    // a filesystem in memory at /tmp
    static fs_mount_t tmpfs;
    if (kerr_failed(tmpfs_mount(&tmpfs)) || kerr_failed(vfs_mount("/tmp", &tmpfs)))
    {
        KLOG_WARN("kernel: tmpfs mount failed; /tmp unavailable");
    }

    // kernel state as text at /proc
    static fs_mount_t procfs;
    if (kerr_failed(procfs_mount(&procfs)) || kerr_failed(vfs_mount("/proc", &procfs)))
    {
        KLOG_WARN("kernel: procfs mount failed; /proc unavailable");
    }

#ifdef KERNEL_TEST
    // test kernel (make test): run the in-kernel tests instead of the shell.
    // ktest_run_all exits QEMU with the result and never returns.
    KLOG_INFO("kernel: root filesystem mounted, running tests");
    ktest_run_all();
#endif

    // the console is the controlling terminal of init's session
    console_terminal_init(PID_INIT);

    // start init as PID 1 through the normal scheduler path. it has no
    // parent; if it ever exits, usermode_exit halts the system. without
    // /bin/init, the shell is PID 1 itself.
    const char *init_argv[] = {"init", NULL};
    const char *shell_argv[] = {"sh", NULL};
    if (exec_program("/bin/init", init_argv) >= 0)
    {
        KLOG_INFO("kernel: root filesystem mounted, launching /bin/init");
    }
    else if (exec_program("/bin/sh", shell_argv) >= 0)
    {
        KLOG_WARN("kernel: no /bin/init; launching /bin/sh as PID 1");
    }
    else
    {
        kernel_panic("failed to launch /bin/init or /bin/sh");
    }

    // hand the CPU to PID 1. the boot context is never resumed: from here on
    // the scheduler runs processes, or the idle process when none is ready.
    scheduler_yield();
    kernel_panic("kernel_main: the boot context was scheduled again");
}

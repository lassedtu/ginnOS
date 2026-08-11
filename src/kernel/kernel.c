#include "kernel.h"

#include "panic.h"
#include "assert.h"
#include "shell/command.h"
#include "shell/shell.h"

#include "fs/fs.h"
#include "hal/hal.h"
#include "vfs/vfs.h"

#include "../common/stdio.h"
#include "../arch/x86/cpu/io.h"

#include "../drivers/disk/ata.h"
#include "../drivers/disk/partition.h"
#include "../drivers/keyboard/keyboard.h"

#include "console/console.h"
#include "memory/kernel_layout.h"
#include "memory/pmm.h"
#include "memory/pmm_layout.h"
#include "memory/region.h"
#include "memory/reservations.h"

void memory_print_map(boot_info_t *boot);

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
    ATA_DEVICE ata;
    PARTITION_DEVICE part;
    FS_MOUNT mount;

    hal_initialize();

    console_initialize();

    stdio_set_putchar(console_putchar); // set stdio output to console (sophisticated VGA text buffer)

    printf("Kernel: entered 32-bit C main\r\n");
    printf("Kernel layout: start=0x%x end=0x%x\r\n",
           kernel_start_address(),
           kernel_end_address());
    printf("kernel size: %u bytes\r\n", kernel_size());

    if (kernel_end_address() <= kernel_start_address())
    {
        kernel_panic("invalid kernel layout");
    }

    memory_reserve_kernel();
    memory_reserve_stage2();

    pmm_layout_init(boot);
    memory_reserve_pmm_bitmap(pmm_bitmap_start(), pmm_bitmap_end());

    region_print_all();

    pmm_init(boot);

    printf("Boot drive: %u\r\n", boot->boot_drive);
    printf("E820 regions: %u\r\n", boot->memory_map.count);

    memory_print_map(boot);

    // enable hardware interrupts (STI).
    // hal_initialize() has fully installed all exception handlers (vectors 0–31),
    // IRQ handlers (vectors 32–47), and device driver handlers. Every gate that
    // can fire is now present and backed by a registered handler. No interrupt
    // can arrive before this point because the CPU holds IF=0 from boot.
    io_enable_interrupts();

    printf("HAL initialized\r\n");

    if (!ATA_Initialize(&ata, ATA_CHANNEL_PRIMARY, ATA_DRIVE_MASTER))
    {
        kernel_panic("ATA initialization failed");
    }

    if (!PARTITION_DetectExt2(&part, &ata.block))
    {
        kernel_panic("EXT2 partition detection failed");
    }

    if (!fs_mount(&mount, &part.block))
    {
        kernel_panic("EXT2 mount failed");
    }

    if (!vfs_mount_root(&mount))
    {
        kernel_panic("VFS root mount failed");
    }

    printf("kernel: mounted EXT2 partition at LBA %u\r\n", part.start_lba);

    commands_initialize();

    shell_run();

    for (;;)
        ;
}

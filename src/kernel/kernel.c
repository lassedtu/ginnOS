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

    (void)boot->boot_drive;

    hal_initialize();

    console_initialize();

    stdio_set_putchar(console_putchar); // set stdio output to console (sophisticated VGA text buffer)

    printf("Kernel: entered 32-bit C main\r\n");
    printf("Boot drive: %u\r\n", boot->boot_drive);

    io_enable_interrupts();

    printf("HAL initialized\r\n");

    if (!ATA_Initialize(&ata))
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

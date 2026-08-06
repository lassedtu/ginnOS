#include "kernel.h"
#include "panic.h"
#include "assert.h"

#include "fs/fs.h"
#include "hal/hal.h"

#include "../common/stdio.h"
#include "../arch/x86/cpu/io.h"

#include "../drivers/disk/ata.h"
#include "../drivers/disk/partition.h"
#include "../drivers/keyboard/keyboard.h"

void kernel_main(void)
{
    ATA_DEVICE ata;
    PARTITION_DEVICE part;
    FS_MOUNT mount;

    printf("Kernel: entered 32-bit C main\r\n");

    hal_initialize();

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

    printf("kernel: mounted EXT2 partition at LBA %u\r\n", part.start_lba);

    // keyboard driver test
    printf("kernel: keyboard driver test: \r\n");
    while (1)
    {
        if (keyboard_available())
        {
            char c = keyboard_getchar();

            printf("%c", c);
        }
    }

    for (;;)
        ;
}

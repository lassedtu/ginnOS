#include "common/stdint.h"
#include "common/stdio.h"
#include "common/boot/boot_info.h"
#include "drivers/disk/ata.h"
#include "drivers/disk/partition.h"
#include "fs/ext2/ext2.h"

#define KERNEL_ENTRY_ADDRESS 0x10000u // address where the kernel binary will be loaded in memory (64KB mark)

extern void puts_char(char c);

static void stage2_putchar(char c)
{
    puts_char(c);
}

void cstart_(boot_info_t *boot)
{
    typedef void (*KernelEntryFn)(boot_info_t *);
    KernelEntryFn kernel_entry;

    ATA_DEVICE ata;
    PARTITION_DEVICE part;
    EXT2_VOLUME volume;
    EXT2_FILE file;
    uint32_t bytes_read;

    if (!boot)
    {
        printf("stage2: error - missing boot_info_t\r\n");
        for (;;)
            ;
    }

    (void)boot->boot_drive;

    stdio_set_putchar(stage2_putchar);

    printf("stage2: entered 32-bit bootloader\r\n");

    if (!ATA_Initialize(&ata, ATA_CHANNEL_PRIMARY, ATA_DRIVE_MASTER))
    {
        printf("stage2: error - ATA initialize failed\r\n");
        for (;;)
            ;
    }

    if (!PARTITION_DetectExt2(&part, &ata.block))
    {
        printf("stage2: error - EXT2 partition detection failed\r\n");
        for (;;)
            ;
    }

    if (!EXT2_Initialize(&volume, &part.block))
    {
        printf("stage2: error - EXT2 volume mount failed\r\n");
        for (;;)
            ;
    }

    if (!EXT2_Open(&volume, "/boot/kernel.bin", &file) &&
        !EXT2_Open(&volume, "/kernel.bin", &file))
    {
        printf("stage2: error - kernel.bin not found on EXT2 volume\r\n");
        for (;;)
            ;
    }

    bytes_read = EXT2_Read(&file, file.size, (void *)KERNEL_ENTRY_ADDRESS);
    EXT2_Close(&file);

    if (bytes_read == 0)
    {
        printf("stage2: error - failed to read kernel bytes\r\n");
        for (;;)
            ;
    }

    kernel_entry = (KernelEntryFn)KERNEL_ENTRY_ADDRESS;
    kernel_entry(boot);

    printf("stage2: kernel returned unexpectedly\r\n");

    for (;;)
        ;
}
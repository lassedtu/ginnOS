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

    ata_device_t ata;
    partition_device_t part;
    ext2_volume_t volume;
    ext2_file_t file;
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

    if (!ata_initialize(&ata, ATA_CHANNEL_PRIMARY, ATA_DRIVE_MASTER))
    {
        printf("stage2: error - ATA initialize failed\r\n");
        for (;;)
            ;
    }

    if (!partition_detect_ext2(&part, &ata.block))
    {
        printf("stage2: error - EXT2 partition detection failed\r\n");
        for (;;)
            ;
    }

    if (kerr_failed(ext2_initialize(&volume, &part.block)))
    {
        printf("stage2: error - EXT2 volume mount failed\r\n");
        for (;;)
            ;
    }

    if (kerr_failed(ext2_open(&volume, "/boot/kernel.bin", &file)) &&
        kerr_failed(ext2_open(&volume, "/kernel.bin", &file)))
    {
        printf("stage2: error - kernel.bin not found on EXT2 volume\r\n");
        for (;;)
            ;
    }

    bytes_read = ext2_read(&file, file.size, (void *)KERNEL_ENTRY_ADDRESS);
    ext2_close(&file);

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
#include "../../common/stdint.h"
#include "../../common/stdio.h"
#include "../../drivers/disk/ata.h"
#include "../../drivers/disk/partition.h"
#include "../../fs/ext2/ext2.h"

#define KERNEL_ENTRY_ADDRESS 0x10000u

void vga_clear_screen(void);

void cstart_(uint16_t bootDrive)
{
    typedef void (*KernelEntryFn)(void);
    KernelEntryFn kernel_entry;

    ATA_DEVICE ata;
    PARTITION_DEVICE part;
    EXT2_VOLUME volume;
    EXT2_FILE file;
    uint32_t bytes_read;

    (void)bootDrive;

    printf("stage2: entered 32-bit bootloader\r\n");

    if (!ATA_Initialize(&ata))
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

    printf("stage2: detected EXT2 partition at LBA %u\r\n", part.start_lba);

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

    printf("stage2: loading kernel from ext2 (%u bytes) to 0x%x...\r\n", file.size, KERNEL_ENTRY_ADDRESS);

    bytes_read = EXT2_Read(&file, file.size, (void *)KERNEL_ENTRY_ADDRESS);
    EXT2_Close(&file);

    if (bytes_read == 0)
    {
        printf("stage2: error - failed to read kernel bytes\r\n");
        for (;;)
            ;
    }

    printf("stage2: jumping to kernel at 0x%x\r\n", KERNEL_ENTRY_ADDRESS);

    kernel_entry = (KernelEntryFn)KERNEL_ENTRY_ADDRESS;
    kernel_entry();

    printf("stage2: kernel returned unexpectedly\r\n");

    for (;;)
        ;
}
#include "kernel.h"
#include "panic.h"
#include "assert.h"
#include "../common/stdio.h"
#include "../arch/x86/cpu/gdt.h"
#include "../drivers/disk/ata.h"
#include "../drivers/disk/partition.h"
#include "fs/fs.h"

void kernel_main(void)
{
    ATA_DEVICE ata;
    PARTITION_DEVICE part;
    FS_MOUNT mount;

    printf("Kernel: entered 32-bit C main\r\n");

    gdt_initialize();

    printf("GDT initialized\r\n");

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

    for (;;)
        ;
}

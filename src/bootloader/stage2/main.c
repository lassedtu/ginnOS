#include "../../common/stdint.h"
#include "../../common/stdio.h"

#define KERNEL_ENTRY_ADDRESS 0x10000u

void cstart_(uint16_t bootDrive)
{
    typedef void (*KernelEntryFn)(void);
    KernelEntryFn kernel_entry;

    (void)bootDrive;

    printf("stage2: jumping to kernel at 0x%x\r\n", KERNEL_ENTRY_ADDRESS);

    kernel_entry = (KernelEntryFn)KERNEL_ENTRY_ADDRESS;
    kernel_entry();

    printf("stage2: kernel returned unexpectedly\r\n");

    for (;;)
        ;
}
#include "panic.h"
#include "common/stdio.h"

void kernel_panic(const char *message)
{
    __asm__ __volatile__("cli"); // disable interrupts

    printf("KERNEL PANIC\r\n");
    printf("Reason: ");
    printf("%s\r\n\r\n", message ? message : "Unknown error");
    printf("System halted.\r\n");

    for (;;)
    {
        __asm__ __volatile__("cli; hlt"); // halt CPU indefinitely
    }
}

#include "panic.h"
#include "arch/arch.h"
#include "common/stdio.h"

void kernel_panic(const char *message)
{
    arch_disable_interrupts();

    printf("KERNEL PANIC\r\n");
    printf("Reason: ");
    printf("%s\r\n\r\n", message ? message : "Unknown error");
    printf("System halted.\r\n");

    arch_halt_forever();
}

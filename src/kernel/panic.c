#include "panic.h"
#include "arch/arch.h"
#include "common/stdio.h"
#include "klog/klog.h"

void kernel_panic(const char *message)
{
    arch_disable_interrupts();

    // mirror to serial so panics are visible headlessly (QEMU -serial),
    // even when the VGA console is unavailable or the fault is early.
    KLOG_ERROR("KERNEL PANIC: %s", message ? message : "Unknown error");

    printf("KERNEL PANIC\r\n");
    printf("Reason: ");
    printf("%s\r\n\r\n", message ? message : "Unknown error");
    printf("System halted.\r\n");

    arch_halt_forever();
}

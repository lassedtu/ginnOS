#include "panic.h"
#include "arch/arch.h"
#include "common/stdio.h"
#include "klog/klog.h"

#ifdef KERNEL_TEST
#include "test/ktest.h"
#endif

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

#ifdef KERNEL_TEST
    // a panic fails the test run immediately instead of hanging until the
    // make test timeout.
    ktest_exit(1);
#endif

    arch_halt_forever();
}

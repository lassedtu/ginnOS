#include "panic.h"
#include "arch/arch.h"
#include "common/stdio.h"
#include "kernel/console/console.h"
#include "kernel/debug/backtrace.h"
#include "klog/klog.h"

#ifdef KERNEL_TEST
#include "test/ktest.h"
#endif

// set once a panic starts, so a fault while printing the backtrace does not
// recurse forever.
static volatile bool panicking;

static void panic_header(const char *message)
{
    // mirror to serial so panics are visible headlessly (QEMU -serial),
    // even when the VGA console is unavailable or the fault is early.
    KLOG_ERROR("KERNEL PANIC: %s", message ? message : "Unknown error");

    printf("KERNEL PANIC\r\n");
    printf("Reason: ");
    printf("%s\r\n\r\n", message ? message : "Unknown error");
}

static void panic_halt(void) __attribute__((noreturn));

static void panic_halt(void)
{
    printf("\r\nSystem halted.\r\n");

#ifdef KERNEL_TEST
    // a panic fails the test run immediately instead of hanging until the
    // make test timeout.
    ktest_exit(1);
#endif

    arch_halt_forever();
}

// true if this is a nested panic; prints a short note in that case.
static bool panic_reentered(const char *message)
{
    if (!panicking)
    {
        panicking = true;
        // the panic may have struck inside the console with its lock held
        console_panic_mode();
        return false;
    }
    KLOG_ERROR("nested panic: %s", message ? message : "Unknown error");
    printf("\r\nnested panic: %s\r\n", message ? message : "Unknown error");
    return true;
}

void kernel_panic(const char *message)
{
    arch_disable_interrupts();

    if (panic_reentered(message))
    {
        panic_halt();
    }

    panic_header(message);
    backtrace_print((uintptr_t)__builtin_frame_address(0));
    panic_halt();
}

void kernel_panic_at(const char *message, uintptr_t pc, uintptr_t caller, uintptr_t frame)
{
    arch_disable_interrupts();

    if (panic_reentered(message))
    {
        panic_halt();
    }

    panic_header(message);
    backtrace_print_pc("at", pc);
    if (caller != 0)
    {
        backtrace_print_return("?", caller);
    }
    if (frame != 0)
    {
        backtrace_print(frame);
    }
    panic_halt();
}

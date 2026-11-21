#include "ktest.h"

#include "arch/arch.h"
#include "common/stdio.h"
#include "common/string.h"
#include "drivers/serial/serial.h"

// x86/QEMU only: the isa-debug-exit device sits on this port when QEMU runs
// with `-device isa-debug-exit,iobase=0xf4,iosize=0x04`. writing v makes QEMU
// exit with status (v << 1) | 1, so 0 -> 1 (pass) and 1 -> 3 (fail).
#include "arch/x86/cpu/io.h"
#define KTEST_EXIT_PORT 0xF4

// linker-provided bounds of the .ktests section (linker/kernel.ld).
extern const ktest_t __start_ktests[];
extern const ktest_t __stop_ktests[];

// failures recorded by the test currently running.
static uint32_t current_failures;

// the test currently running, so a panic can report which test died.
static const ktest_t *current_test;

bool ktest_streq_(const char *a, const char *b)
{
    if (!a || !b)
        return a == b;
    return strcmp(a, b) == 0;
}

void ktest_fail(const char *file, int line, const char *expr)
{
    current_failures++;
    stdio_printf_to(serial_putchar, "    %s:%d: expected %s\n", file, line, expr);
}

void ktest_fail_eq(const char *file, int line, const char *lhs, const char *rhs,
                   uint32_t lhs_val, uint32_t rhs_val)
{
    current_failures++;
    stdio_printf_to(serial_putchar, "    %s:%d: expected %s == %s (0x%x vs 0x%x)\n",
                    file, line, lhs, rhs, lhs_val, rhs_val);
}

void ktest_fail_streq(const char *file, int line, const char *lhs, const char *rhs,
                      const char *lhs_val, const char *rhs_val)
{
    current_failures++;
    stdio_printf_to(serial_putchar, "    %s:%d: expected %s == %s (\"%s\" vs \"%s\")\n",
                    file, line, lhs, rhs,
                    lhs_val ? lhs_val : "(null)", rhs_val ? rhs_val : "(null)");
}

void ktest_exit(uint32_t failures)
{
    // still inside a test means we got here from kernel_panic.
    if (current_test)
    {
        stdio_printf_to(serial_putchar, "  FAIL %s (kernel panic)\n", current_test->name);
    }

    io_outb(KTEST_EXIT_PORT, failures ? 1 : 0);

    // only reached without the debug-exit device.
    arch_halt_forever();
}

void ktest_run_all(void)
{
    uint32_t total = (uint32_t)(__stop_ktests - __start_ktests);
    uint32_t failed_tests = 0;

    stdio_printf_to(serial_putchar, "KTEST: running %u tests\n", total);

    for (const ktest_t *t = __start_ktests; t < __stop_ktests; t++)
    {
        current_failures = 0;
        current_test = t;
        t->fn();
        current_test = NULL;

        if (current_failures == 0)
        {
            stdio_printf_to(serial_putchar, "  PASS %s\n", t->name);
        }
        else
        {
            stdio_printf_to(serial_putchar, "  FAIL %s (%u failed expectations)\n",
                            t->name, current_failures);
            failed_tests++;
        }
    }

    stdio_printf_to(serial_putchar, "KTEST: %u passed, %u failed\n",
                    total - failed_tests, failed_tests);

    ktest_exit(failed_tests);
}

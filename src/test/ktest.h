#pragma once

/**
 * in-kernel test framework.
 *
 * tests live under src/test/ and are only compiled into the test kernel
 * (`make test`), never into the normal kernel. each test is registered with
 * KTEST(name) { ... }, which places a descriptor in the .ktests linker section
 * (see linker/kernel.ld). the test kernel boots normally, mounts the root
 * filesystem, then runs every registered test instead of launching /bin/sh.
 *
 * results go to the serial port. the run ends by writing to QEMU's
 * isa-debug-exit device, so `make test` gets a real pass/fail exit code.
 * a failed KEXPECT records the failure and lets the test continue, so one
 * run reports every broken expectation, not just the first.
 */

#include "common/stdint.h"

/**
 * a registered test: its name and body.
 */
typedef struct
{
    const char *name;
    void (*fn)(void);
} ktest_t;

/**
 * define and register a test. use as: KTEST(heap_alloc) { ... }
 * the descriptor is marked used so the linker keeps it even though nothing
 * references it by name.
 */
#define KTEST(test_name)                                                     \
    static void ktest_fn_##test_name(void);                                  \
    static const ktest_t ktest_desc_##test_name                              \
        __attribute__((used, section(".ktests"), aligned(4))) = {            \
            #test_name, ktest_fn_##test_name};                               \
    static void ktest_fn_##test_name(void)

// record a failed expectation in the current test (prefer the macros below).
void ktest_fail(const char *file, int line, const char *expr);
void ktest_fail_eq(const char *file, int line, const char *lhs, const char *rhs,
                   uint32_t lhs_val, uint32_t rhs_val);
void ktest_fail_streq(const char *file, int line, const char *lhs, const char *rhs,
                      const char *lhs_val, const char *rhs_val);

/**
 * expect a condition to hold; on failure, log it and keep running.
 */
#define KEXPECT(cond)                                  \
    do                                                 \
    {                                                  \
        if (!(cond))                                   \
            ktest_fail(__FILE__, __LINE__, #cond);     \
    } while (0)

/**
 * expect two integer values to be equal; logs both values on failure.
 * values are compared and printed as uint32_t.
 */
#define KEXPECT_EQ(a, b)                                                     \
    do                                                                       \
    {                                                                        \
        uint32_t ktest_a_ = (uint32_t)(a);                                   \
        uint32_t ktest_b_ = (uint32_t)(b);                                   \
        if (ktest_a_ != ktest_b_)                                            \
            ktest_fail_eq(__FILE__, __LINE__, #a, #b, ktest_a_, ktest_b_);   \
    } while (0)

/**
 * expect two strings to be equal; logs both strings on failure.
 */
#define KEXPECT_STREQ(a, b)                                                  \
    do                                                                       \
    {                                                                        \
        const char *ktest_a_ = (a);                                          \
        const char *ktest_b_ = (b);                                          \
        if (!ktest_streq_(ktest_a_, ktest_b_))                               \
            ktest_fail_streq(__FILE__, __LINE__, #a, #b, ktest_a_, ktest_b_); \
    } while (0)

// NULL-safe string equality used by KEXPECT_STREQ.
bool ktest_streq_(const char *a, const char *b);

/**
 * run every registered test, log a summary, and exit QEMU with the result.
 * never returns.
 */
void ktest_run_all(void) __attribute__((noreturn));

/**
 * exit QEMU through the isa-debug-exit device: 0 = all passed, nonzero =
 * failure. halts if the device is absent (e.g. booted without it).
 * also called by kernel_panic in the test kernel so a crash fails the run
 * immediately instead of hanging until the timeout.
 */
void ktest_exit(uint32_t failures) __attribute__((noreturn));

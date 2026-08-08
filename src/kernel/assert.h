#pragma once

#include "panic.h"

/**
 * Assert that a condition is true. If false, trigger a kernel panic.
 * @param condition boolean expression to test.
 *
 * This macro is NOT compiled out in any build configuration. It expands to a
 * direct call to kernel_panic(), which is marked __attribute__((noreturn)) and
 * halts the CPU unconditionally via an infinite cli/hlt loop. There is no
 * NDEBUG or release-mode stripping — out-of-bounds writes into hardware-
 * consulted structures (IDT, ISR/IRQ handler tables) are treated as fatal
 * regardless of build type.
 */
#define ASSERT(condition)                                   \
    do                                                      \
    {                                                       \
        if (!(condition))                                   \
        {                                                   \
            kernel_panic("Assertion failed: " #condition); \
        }                                                   \
    } while (0)

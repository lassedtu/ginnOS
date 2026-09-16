#pragma once

#include "panic.h"

/**
 * assert that a condition is true. if false, trigger a kernel panic.
 * @param condition boolean expression to test.
 *
 * this macro is NOT compiled out in any build configuration. it expands to a
 * direct call to kernel_panic(), which halts the CPU unconditionally.
 * use for invariants that must hold regardless of build type.
 */
#define ASSERT(condition)                                  \
    do                                                     \
    {                                                      \
        if (!(condition))                                  \
        {                                                  \
            kernel_panic("Assertion failed: " #condition); \
        }                                                  \
    } while (0)

/**
 * debug-only assertion. stripped when NDEBUG is defined (release builds).
 * use for expensive checks or invariants only needed during development.
 *
 * @param condition boolean expression to test.
 * @param msg string literal describing the failure.
 */
#ifdef NDEBUG
#define KASSERT(condition, msg) ((void)0)
#else
#define KASSERT(condition, msg)                               \
    do                                                       \
    {                                                        \
        if (!(condition))                                    \
        {                                                    \
            kernel_panic("KASSERT failed: " msg              \
                         " (" #condition ")");               \
        }                                                    \
    } while (0)
#endif

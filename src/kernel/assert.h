#pragma once

#include "panic.h"

/**
 * assert that a condition is true. if false, trigger a kernel panic.
 * @param condition boolean expression to test.
 */
#define ASSERT(condition)                               \
    do                                                  \
    {                                                   \
        if (!(condition))                               \
        {                                               \
            kernel_panic("Assertion failed: " #condition); \
        }                                               \
    } while (0)

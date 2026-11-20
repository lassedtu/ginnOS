#pragma once

/**
 * fixed-width integer types.
 */
// fixed-width types from the compiler's own macros, so they have the right
// width on every target (on i686 int32_t is long, on x86_64 it is int).
typedef __INT8_TYPE__ int8_t;
typedef __UINT8_TYPE__ uint8_t;
typedef __INT16_TYPE__ int16_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT32_TYPE__ int32_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT64_TYPE__ int64_t;
typedef __UINT64_TYPE__ uint64_t;

// integers wide enough to hold a pointer: the only way pointers and integers
// should be converted. the compiler names the right type for each target.
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;

#define UINTPTR_MAX __UINTPTR_MAX__

typedef uint8_t bool;

#define false 0
#define true 1

#include "common/stddef.h"

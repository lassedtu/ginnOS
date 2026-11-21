#pragma once

/**
 * fixed-width integer types.
 */
// fixed-width types, pinned to the same C types on every target we build
// for (i686 ILP32 and x86_64 LP64), so one printf format fits each:
// %u/%x for uint32_t, %llu/%llx for uint64_t. pointer-width types below
// (uintptr_t, size_t) are unsigned long on both: %lx, %zu.
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

_Static_assert(sizeof(int32_t) == 4 && sizeof(uint64_t) == 8, "fixed-width types");

// integers wide enough to hold a pointer: the only way pointers and integers
// should be converted. the compiler names the right type for each target.
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;

#define UINTPTR_MAX __UINTPTR_MAX__
#define UINT16_MAX 0xFFFF
#define UINT32_MAX 0xFFFFFFFFu

typedef uint8_t bool;

#define false 0
#define true 1

#include "common/stddef.h"

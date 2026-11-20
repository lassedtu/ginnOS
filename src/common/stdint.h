#pragma once

/**
 * fixed-width integer types.
 */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed long int int32_t;
typedef unsigned long int uint32_t;
typedef signed long long int int64_t;
typedef unsigned long long int uint64_t;

// integers wide enough to hold a pointer: the only way pointers and integers
// should be converted. the compiler names the right type for each target.
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;

#define UINTPTR_MAX __UINTPTR_MAX__

typedef uint8_t bool;

#define false 0
#define true 1

#include "common/stddef.h"

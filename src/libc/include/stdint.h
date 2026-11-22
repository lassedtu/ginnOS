#pragma once

/**
 * @file stdint.h
 * @brief integer types of fixed width. the types are the compiler's own
 *        (__INT64_TYPE__ and so on), so they agree with it on both
 *        architectures: int64_t is long long on i686 and long on x86_64.
 */

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef __INT64_TYPE__ int64_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTMAX_TYPE__ intmax_t;
typedef __UINTMAX_TYPE__ uintmax_t;

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  2147483647
#define UINT32_MAX 4294967295u
#define INT64_MAX  __INT64_MAX__
#define INT64_MIN  (-INT64_MAX - 1)
#define UINT64_MAX __UINT64_MAX__
#define INTMAX_MAX  __INTMAX_MAX__
#define INTMAX_MIN  (-INTMAX_MAX - 1)
#define UINTMAX_MAX __UINTMAX_MAX__
#define INT64_C(c)  __INT64_C(c)
#define UINT64_C(c) __UINT64_C(c)
#define INTPTR_MAX  __INTPTR_MAX__
#define UINTPTR_MAX __UINTPTR_MAX__
#define SIZE_MAX    __SIZE_MAX__

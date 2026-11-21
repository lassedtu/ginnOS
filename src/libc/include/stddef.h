#pragma once

#include <sys/types.h>

/**
 * @file stddef.h
 * @brief NULL, size_t, ptrdiff_t, offsetof.
 */

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __WCHAR_TYPE__ wchar_t;

#define offsetof(type, member) __builtin_offsetof(type, member)

#pragma once

/**
 * @file stddef.h
 * @brief NULL, size_t, ptrdiff_t, offsetof.
 */

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef unsigned int size_t;
typedef int ptrdiff_t;
typedef int wchar_t;

#define offsetof(type, member) __builtin_offsetof(type, member)

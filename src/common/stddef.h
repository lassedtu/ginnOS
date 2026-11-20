#pragma once

// object sizes and byte counts (unsigned), byte counts or an error (signed),
// and pointer differences. pointer-width on every target.
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __INTPTR_TYPE__ ssize_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

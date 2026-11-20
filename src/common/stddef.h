#pragma once

// object sizes and byte counts (unsigned), byte counts or an error (signed),
// and pointer differences. pointer-width on every target.
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __INTPTR_TYPE__ ssize_t;

// byte offset of a member within a struct
#define offsetof(type, member) __builtin_offsetof(type, member)

#ifndef NULL
#define NULL ((void *)0)
#endif

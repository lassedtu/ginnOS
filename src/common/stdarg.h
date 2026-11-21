#pragma once

/**
 * variadic arguments, from the compiler's builtins. works for every calling
 * convention (stack-passed on i686, register-passed on x86_64), unlike
 * walking a pointer from the last named parameter.
 */

typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dst, src) __builtin_va_copy(dst, src)

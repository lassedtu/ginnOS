#pragma once

/**
 * @file stdio.h
 * @brief minimal stdio for ginnOS libc.
 *
 * supports: putchar, puts, printf, snprintf, vsnprintf.
 * format specifiers: %d, %i, %u, %x, %X, %s, %c, %%.
 */

typedef unsigned int size_t;

/* variadic argument support */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(dst, src)  __builtin_va_copy(dst, src)

int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * write formatted output to a buffer with a size limit.
 * @param buf destination buffer.
 * @param size buffer size (including space for null terminator).
 * @param fmt format string.
 * @return number of characters that would have been written (excluding null),
 *         regardless of size limit (like C99 snprintf).
 */
int snprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * write formatted output to a buffer with a size limit (va_list version).
 * @param buf destination buffer.
 * @param size buffer size (including space for null terminator).
 * @param fmt format string.
 * @param ap argument list.
 * @return number of characters that would have been written (excluding null).
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

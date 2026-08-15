#pragma once

/**
 * minimal stdio for ginnOS libc.
 * supports: putchar, puts, printf (%s, %d, %u, %x, %c, %%).
 */

int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

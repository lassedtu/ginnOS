#pragma once

#include "stdint.h"

/**
 * print a single character to the output.
 * @param c character to print.
 */
void puts_char(char c);

/**
 * print a null terminated string to the output.
 * @param str null terminated string to print.
 */
void puts(const char *str);

/**
 * print a formatted string to the output.
 * @param fmt format string.
 */
void printf(const char *fmt, ...);

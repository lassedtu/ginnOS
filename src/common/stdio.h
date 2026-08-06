#pragma once

#include "stdint.h"

/**
 * signature of a low-level character output function used by stdio.
 * @param c character to output.
 */
typedef void (*StdioPutCharFn)(char c);

/**
 * configure the low-level output backend for stdio.
 * this must be called by each runtime environment before printf/puts are used.
 * @param func callback used for character output.
 */
void stdio_set_putchar(StdioPutCharFn func);

/**
 * output a single character through the configured stdio backend.
 * if no backend is configured this function does nothing.
 * @param c character to output.
 */
void stdio_putchar(char c);

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

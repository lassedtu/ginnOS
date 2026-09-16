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

/**
 * print a formatted string through a caller-supplied output backend,
 * bypassing the global stdio backend. lets subsystems (e.g. klog) reuse
 * the printf formatter to write somewhere other than the console.
 * @param out  character output callback.
 * @param fmt  format string.
 */
void stdio_printf_to(StdioPutCharFn out, const char *fmt, ...);

/**
 * format engine entry point for callers that already hold a pointer to the
 * variadic argument list on their own stack frame (obtained as
 * `(int *)&fmt` advanced past the last named parameter).
 *
 * this exists because printf-style forwarding isn't possible with the
 * manual argument-walking used here; a variadic function that wants to
 * reuse the formatter passes its own argp instead of forwarding `...`.
 * @param out  character output callback.
 * @param fmt  format string.
 * @param argp pointer to the first variadic argument.
 */
void stdio_format_args(StdioPutCharFn out, const char *fmt, int *argp);

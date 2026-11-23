/**
 * @file fmt_fp.h
 * @brief the float conversions of printf (fmt_fp.c), for stdio.c.
 */

#ifndef GINNOS_FMT_FP_H
#define GINNOS_FMT_FP_H

#include <stddef.h>

/** where format_float writes: write(context, text, length) */
struct float_out
{
    void (*write)(void *context, const char *text, size_t length);
    void *context;
};

// the printf flags, as musl numbers them (a bit for each flag character)
#define FLOAT_ALT   (1U << ('#' - ' '))
#define FLOAT_ZERO  (1U << ('0' - ' '))
#define FLOAT_LEFT  (1U << ('-' - ' '))
#define FLOAT_SPACE (1U << (' ' - ' '))
#define FLOAT_PLUS  (1U << ('+' - ' '))

/**
 * write value as printf's conversion ('e', 'f', 'g', 'a' or a capital
 * one) with the width, the precision (-1: none) and the FLOAT_ flags.
 * @return the number of bytes written, or -1 if it is too long for int.
 */
int format_float(struct float_out *out, long double value, int width, int precision,
                 unsigned int flags, int conversion);

#endif

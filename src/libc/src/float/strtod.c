/**
 * @file strtod.c
 * @brief strtod, strtof and strtold through musl's floatscan.c.
 *
 * ginnOS's own. floatscan rounds correctly, and reads hexadecimal
 * numbers ("0x1.8p3"), "inf", "infinity" and "nan" too. it sets errno
 * to ERANGE for a number past the largest or below the smallest.
 */

#include <stdio.h>
#include <stdlib.h>

#include "shgetc.h"
#include "floatscan.h"

/** read a number of the precision (0 float, 1 double, 2 long double) */
static long double read_number(const char *text, char **end, int precision)
{
    struct scan_source source = {(const unsigned char *)text, (const unsigned char *)text};
    long double value = __floatscan(&source, precision, 1);
    long count = shcnt(&source);
    if (end)
    {
        // no number: shlim made the count 0, and end is the text itself
        *end = (char *)text + count;
    }
    return value;
}

float strtof(const char *text, char **end)
{
    return (float)read_number(text, end, 0);
}

double strtod(const char *text, char **end)
{
    return (double)read_number(text, end, 1);
}

long double strtold(const char *text, char **end)
{
    return read_number(text, end, 2);
}

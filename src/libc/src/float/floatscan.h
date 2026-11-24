/**
 * @file floatscan.h
 * @brief musl's number reader (floatscan.c), as the string functions use it.
 *
 * ginnOS's own. include it after shgetc.h, which makes FILE a string
 * source.
 */

#ifndef GINNOS_FLOATSCAN_H
#define GINNOS_FLOATSCAN_H

/**
 * read a number: decimal or hexadecimal, "inf", "infinity" or "nan",
 * rounded correctly to the precision.
 * @param prec 0 for float, 1 for double, 2 for long double.
 * @param pok 1: a "-" or "+" is allowed first.
 * @return the number; on no number, 0 with the source at its start.
 */
long double __floatscan(FILE *f, int prec, int pok);

#endif

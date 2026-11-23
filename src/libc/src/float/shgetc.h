/**
 * @file shgetc.h
 * @brief the reading helpers that musl's floatscan.c uses, for a string.
 *
 * ginnOS's own (musl's shgetc.h reads from a FILE too). floatscan.c
 * includes <stdio.h> first, so FILE here is a string source and not a
 * stream. as in musl, shgetc gives the NUL at the end as 0 and moves on,
 * and shunget moves back without a limit. floatscan calls shlim only when
 * it finds no number: shcnt then counts from there, so it is 0.
 */

#ifndef GINNOS_SHGETC_H
#define GINNOS_SHGETC_H

/** a string that floatscan.c reads */
struct scan_source
{
    const unsigned char *start;
    const unsigned char *pos;
};

#define FILE struct scan_source
#define hidden

#define shlim(f, lim) ((void)(lim), (void)((f)->start = (f)->pos))
#define shgetc(f) (*(f)->pos++)
#define shunget(f) ((void)(f)->pos--)
#define shcnt(f) ((f)->pos - (f)->start)

#endif

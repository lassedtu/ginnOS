#pragma once

/**
 * @file inttypes.h
 * @brief printf formats for the stdint.h types.
 */

#include <stdint.h>

// the length letters of int64_t and of pointers: long on x86_64
#if __SIZEOF_LONG__ == 8
#define __PRI64  "l"
#define __PRIPTR "l"
#else
#define __PRI64  "ll"
#define __PRIPTR ""
#endif

#define PRId8  "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 __PRI64 "d"
#define PRIu8  "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 __PRI64 "u"
#define PRIx8  "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 __PRI64 "x"
#define PRIX32 "X"
#define PRIX64 __PRI64 "X"
#define PRIdPTR __PRIPTR "d"
#define PRIuPTR __PRIPTR "u"
#define PRIxPTR __PRIPTR "x"
#define PRIdMAX __PRI64 "d"
#define PRIuMAX __PRI64 "u"

intmax_t strtoimax(const char *s, char **end, int base);
uintmax_t strtoumax(const char *s, char **end, int base);

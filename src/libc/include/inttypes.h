#pragma once

/**
 * @file inttypes.h
 * @brief printf formats for the stdint.h types.
 */

#include <stdint.h>

#define PRId8  "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "lld"
#define PRIu8  "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 "llu"
#define PRIx8  "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 "llx"
#define PRIX32 "X"
#define PRIX64 "llX"
#define PRIdPTR "d"
#define PRIuPTR "u"
#define PRIxPTR "x"
#define PRIdMAX "lld"
#define PRIuMAX "llu"

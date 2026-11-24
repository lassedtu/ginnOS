#pragma once

/**
 * @file fnmatch.h
 * @brief shell patterns ("*.c", "[a-z]?"): fnmatch is musl's (see
 *        src/libc/src/regex/README.md).
 */

#define FNM_PATHNAME    0x01 // "*" and "?" do not match "/"
#define FNM_NOESCAPE    0x02 // "\" is an ordinary character
#define FNM_PERIOD      0x04 // a "." at the start must be matched by a "."
#define FNM_LEADING_DIR 0x08 // the pattern may match only the folders at the start
#define FNM_CASEFOLD    0x10 // upper and lower case match each other
#define FNM_FILE_NAME   FNM_PATHNAME

#define FNM_NOMATCH 1
#define FNM_NOSYS   (-1)

/** 0 when name matches pattern, FNM_NOMATCH when it does not */
int fnmatch(const char *pattern, const char *name, int flags);

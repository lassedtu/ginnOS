#pragma once

/**
 * @file regex.h
 * @brief POSIX regular expressions, basic (BRE) and extended (ERE). The
 *        matcher is musl's (from TRE): see src/libc/src/regex/README.md.
 */

#include <stddef.h>
#include <sys/types.h>

typedef ssize_t regoff_t;

/** a compiled expression (the layout is musl's) */
typedef struct re_pattern_buffer
{
    size_t re_nsub; // the parenthesised parts
    void *__opaque;
    void *__padding[4];
    size_t __nsub2;
    char __padding2;
} regex_t;

/** where a match (or a part of it) starts and ends; -1 when it did not match */
typedef struct
{
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

// regcomp flags
#define REG_EXTENDED 1 // extended syntax (ERE), not basic
#define REG_ICASE    2 // upper and lower case match each other
#define REG_NEWLINE  4 // a newline ends a line for ^, $ and .
#define REG_NOSUB    8 // only if it matches, not where

// regexec flags
#define REG_NOTBOL 1 // the text does not start a line
#define REG_NOTEOL 2 // the text does not end a line

// results
#define REG_OK       0
#define REG_NOMATCH  1
#define REG_BADPAT   2
#define REG_ECOLLATE 3
#define REG_ECTYPE   4
#define REG_EESCAPE  5
#define REG_ESUBREG  6
#define REG_EBRACK   7
#define REG_EPAREN   8
#define REG_EBRACE   9
#define REG_BADBR    10
#define REG_ERANGE   11
#define REG_ESPACE   12
#define REG_BADRPT   13
#define REG_ENOSYS   -1

/** compile pattern into preg: REG_OK, or an error for regerror */
int regcomp(regex_t *restrict preg, const char *restrict pattern, int cflags);
/**
 * match text against preg: REG_OK or REG_NOMATCH. pmatch gets where the
 * match and the first nmatch - 1 parts are.
 */
int regexec(const regex_t *restrict preg, const char *restrict text, size_t nmatch,
             regmatch_t *restrict pmatch, int eflags);
void regfree(regex_t *preg);
/**
 * the message for an error into buffer (size bytes, cut if it is longer).
 * @return the size the whole message needs, with its null byte.
 */
size_t regerror(int error, const regex_t *restrict preg, char *restrict buffer, size_t size);

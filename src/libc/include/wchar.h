#pragma once

/**
 * @file wchar.h
 * @brief wide characters. ginnOS has one multibyte encoding, UTF-8: a
 *        wchar_t is a Unicode code point.
 */

#include <stddef.h>
#include <stdint.h>

typedef unsigned int wint_t;

#define WEOF ((wint_t)-1)

#ifndef WCHAR_MIN
#define WCHAR_MIN (-2147483647 - 1)
#define WCHAR_MAX 2147483647
#endif

/** the state of a conversion that stopped in a character (UTF-8 needs none) */
typedef struct
{
    unsigned int pending; // bytes of a character still to come (not used)
} mbstate_t;

/**
 * the character at s, of at most n bytes, into *wc (if wc is not NULL).
 * @return the bytes it used, 0 for the null character, (size_t)-1 for
 *         bytes that are not UTF-8 (errno EILSEQ), (size_t)-2 when n
 *         stops in the character.
 */
size_t mbrtowc(wchar_t *wc, const char *s, size_t n, mbstate_t *state);
/** as mbrtowc with s as a whole string: (size_t)-1 or the length */
size_t mbrlen(const char *s, size_t n, mbstate_t *state);
/** wc as UTF-8 at s (MB_CUR_MAX bytes of room): the bytes, or (size_t)-1 */
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *state);
/** always 1: no conversion stops in a character */
int mbsinit(const mbstate_t *state);
/** a character of one byte as a wide character, or WEOF for a byte from 0x80 */
wint_t btowc(int c);
/** a wide character of one byte as that byte, else EOF */
int wctob(wint_t wc);

size_t mbstowcs(wchar_t *dst, const char *src, size_t n);
size_t wcstombs(char *dst, const wchar_t *src, size_t n);

size_t wcslen(const wchar_t *s);
int wcscmp(const wchar_t *a, const wchar_t *b);
int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n);
wchar_t *wcscpy(wchar_t *dst, const wchar_t *src);
wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);
int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n);
wchar_t *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wmemset(wchar_t *dst, wchar_t c, size_t n);

/** the columns a character takes in a terminal: 0, 1 or 2; -1 if it cannot be shown */
int wcwidth(wchar_t wc);

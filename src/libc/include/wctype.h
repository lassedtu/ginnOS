#pragma once

/**
 * @file wctype.h
 * @brief the classes of wide characters (Unicode code points).
 *
 * the ASCII characters are as in <ctype.h>. above ASCII, ginnOS knows the
 * letters (and their case) of Latin-1, Latin Extended-A and B, Greek and
 * Cyrillic, and the letters of the large scripts (CJK, kana, Hangul)
 * without case; the other characters from U+00A0 are printable
 * punctuation. this is enough for regular expressions and terminals.
 */

#include <wchar.h>

typedef int wctype_t; // a class: 0 is none
typedef int wctrans_t; // 1 tolower, 2 toupper, 0 none

int iswalnum(wint_t wc);
int iswalpha(wint_t wc);
int iswblank(wint_t wc);
int iswcntrl(wint_t wc);
int iswdigit(wint_t wc);
int iswgraph(wint_t wc);
int iswlower(wint_t wc);
int iswprint(wint_t wc);
int iswpunct(wint_t wc);
int iswspace(wint_t wc);
int iswupper(wint_t wc);
int iswxdigit(wint_t wc);
wint_t towlower(wint_t wc);
wint_t towupper(wint_t wc);

/** a class by name ("alpha", "digit", ...), or 0 */
wctype_t wctype(const char *name);
int iswctype(wint_t wc, wctype_t type);
/** a mapping by name ("tolower", "toupper"), or 0 */
wctrans_t wctrans(const char *name);
wint_t towctrans(wint_t wc, wctrans_t trans);

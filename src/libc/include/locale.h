#pragma once

/**
 * @file locale.h
 * @brief locales. ginnOS has one: "C" (also called "POSIX"). text is
 *        UTF-8 bytes, numbers use '.', and nothing is translated.
 */

#include <stddef.h>

#define LC_CTYPE    0
#define LC_NUMERIC  1
#define LC_TIME     2
#define LC_COLLATE  3
#define LC_MONETARY 4
#define LC_MESSAGES 5
#define LC_ALL      6

/**
 * how numbers and money are written. in the "C" locale the strings are
 * empty (no grouping, no currency) except decimal_point, and the char
 * fields are CHAR_MAX (not given).
 */
struct lconv
{
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
    char int_p_cs_precedes;
    char int_p_sep_by_space;
    char int_n_cs_precedes;
    char int_n_sep_by_space;
    char int_p_sign_posn;
    char int_n_sign_posn;
};

/**
 * choose the locale of a category, or ask for it.
 * @param locale "C", "POSIX" or "" (the default, also "C"); NULL asks.
 * @return "C", or NULL if that locale is not there (nothing changes).
 */
char *setlocale(int category, const char *locale);

/**
 * @return the number and money formats of the locale ("C").
 */
struct lconv *localeconv(void);

#include "langinfo.h"
#include "locale.h"

#include "limits.h"
#include "string.h"

/**
 * @file locale.c
 * @brief the one locale, "C".
 */

char *setlocale(int category, const char *locale)
{
    static char name[] = "C";
    if (category < LC_CTYPE || category > LC_ALL)
        return NULL;
    if (!locale || !*locale || strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0)
        return name;
    return NULL;
}

struct lconv *localeconv(void)
{
    static char point[] = ".";
    static char empty[] = "";
    static struct lconv c = {
        .decimal_point = point,
        .thousands_sep = empty,
        .grouping = empty,
        .int_curr_symbol = empty,
        .currency_symbol = empty,
        .mon_decimal_point = empty,
        .mon_thousands_sep = empty,
        .mon_grouping = empty,
        .positive_sign = empty,
        .negative_sign = empty,
        .int_frac_digits = CHAR_MAX,
        .frac_digits = CHAR_MAX,
        .p_cs_precedes = CHAR_MAX,
        .p_sep_by_space = CHAR_MAX,
        .n_cs_precedes = CHAR_MAX,
        .n_sep_by_space = CHAR_MAX,
        .p_sign_posn = CHAR_MAX,
        .n_sign_posn = CHAR_MAX,
        .int_p_cs_precedes = CHAR_MAX,
        .int_p_sep_by_space = CHAR_MAX,
        .int_n_cs_precedes = CHAR_MAX,
        .int_n_sep_by_space = CHAR_MAX,
        .int_p_sign_posn = CHAR_MAX,
        .int_n_sign_posn = CHAR_MAX,
    };
    return &c;
}

char *nl_langinfo(nl_item item)
{
    static const char *const items[] = {
        [CODESET] = "UTF-8",
        [D_T_FMT] = "%a %b %e %H:%M:%S %Y",
        [D_FMT] = "%m/%d/%y",
        [T_FMT] = "%H:%M:%S",
        [T_FMT_AMPM] = "%I:%M:%S %p",
        [AM_STR] = "AM",
        [PM_STR] = "PM",
        [RADIXCHAR] = ".",
        [THOUSEP] = "",
        [YESEXPR] = "^[yY]",
        [NOEXPR] = "^[nN]",
    };
    if (item < 0 || item > NOEXPR)
    {
        return (char *)"";
    }
    return (char *)items[item];
}

<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;locale.h&gt;

Source: [`src/libc/include/locale.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/locale.h)

locales. ginnOS has one: "C" (also called "POSIX"). text is
       UTF-8 bytes, numbers use '.', and nothing is translated.

## lconv

```c
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
```

how numbers and money are written. in the "C" locale the strings are
empty (no grouping, no currency) except decimal\_point, and the char
fields are CHAR\_MAX (not given).

## setlocale

```c
char *setlocale(int category, const char *locale);
```

choose the locale of a category, or ask for it.

- `locale`: "C", "POSIX" or "" (the default, also "C"); NULL asks.

Returns: "C", or NULL if that locale is not there (nothing changes).

## localeconv

```c
struct lconv *localeconv(void);
```

Returns: the number and money formats of the locale ("C").

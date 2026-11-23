/**
 * @file wctype.c
 * @brief the classes and the case of wide characters (see wctype.h for
 *        what ginnOS knows above ASCII).
 */

#include "ctype.h"
#include "string.h"
#include "wctype.h"

/** an upper case letter and its lower case are a fixed distance apart */
typedef struct
{
    wint_t first; // the first upper case letter
    wint_t last;
    int distance; // lower = upper + distance
    int step;     // 1: a block of upper case letters; 2: upper and lower in turn
} case_range_t;

static const case_range_t cases[] = {
    {0x00C0, 0x00D6, 32, 1},  {0x00D8, 0x00DE, 32, 1},  // Latin-1
    {0x0100, 0x012E, 1, 2},   {0x0132, 0x0136, 1, 2},   // Latin Extended-A
    {0x0139, 0x0147, 1, 2},   {0x014A, 0x0176, 1, 2},   {0x0179, 0x017D, 1, 2},
    {0x0182, 0x0184, 1, 2},   {0x01CD, 0x01DB, 1, 2},   // Latin Extended-B, some
    {0x01DE, 0x01EE, 1, 2},   {0x01F8, 0x021E, 1, 2},   {0x0222, 0x0232, 1, 2},
    {0x0391, 0x03A1, 32, 1},  {0x03A3, 0x03AB, 32, 1},  // Greek
    {0x0400, 0x040F, 80, 1},  {0x0410, 0x042F, 32, 1},  // Cyrillic
    {0x0460, 0x0480, 1, 2},   {0x048A, 0x04BE, 1, 2},   {0x04D0, 0x04FE, 1, 2},
};

#define CASE_COUNT ((int)(sizeof(cases) / sizeof(cases[0])))

/** a letter of a script with no case: CJK, kana, Hangul and the like */
static int caseless_letter(wint_t wc)
{
    return (wc >= 0x05D0 && wc <= 0x05EA) || // Hebrew
           (wc >= 0x0620 && wc <= 0x064A) || // Arabic
           (wc >= 0x0E01 && wc <= 0x0E30) || // Thai
           (wc >= 0x3041 && wc <= 0x3096) || // Hiragana
           (wc >= 0x30A1 && wc <= 0x30FA) || // Katakana
           (wc >= 0x3400 && wc <= 0x4DBF) || (wc >= 0x4E00 && wc <= 0x9FFF) || // CJK
           (wc >= 0xAC00 && wc <= 0xD7A3) || // Hangul
           (wc >= 0x20000 && wc <= 0x3FFFD);
}

static int range_upper(const case_range_t *r, wint_t wc)
{
    return wc >= r->first && wc <= r->last && (wc - r->first) % (wint_t)r->step == 0;
}

static int range_lower(const case_range_t *r, wint_t wc)
{
    wint_t upper = wc - (wint_t)r->distance;
    return wc >= (wint_t)r->distance && range_upper(r, upper);
}

int iswupper(wint_t wc)
{
    if (wc < 0x80)
        return isupper((int)wc);
    for (int i = 0; i < CASE_COUNT; i++)
        if (range_upper(&cases[i], wc))
            return 1;
    return 0;
}

int iswlower(wint_t wc)
{
    if (wc < 0x80)
        return islower((int)wc);
    if (wc == 0x00DF || wc == 0x00FF || wc == 0x0131 || wc == 0x0138 || wc == 0x017F)
        return 1; // lower case with no upper case here (or one far away)
    for (int i = 0; i < CASE_COUNT; i++)
        if (range_lower(&cases[i], wc))
            return 1;
    return 0;
}

wint_t towupper(wint_t wc)
{
    if (wc < 0x80)
        return (wint_t)toupper((int)wc);
    if (wc == 0x00FF)
        return 0x0178;
    for (int i = 0; i < CASE_COUNT; i++)
        if (range_lower(&cases[i], wc))
            return wc - (wint_t)cases[i].distance;
    return wc;
}

wint_t towlower(wint_t wc)
{
    if (wc < 0x80)
        return (wint_t)tolower((int)wc);
    if (wc == 0x0178)
        return 0x00FF;
    for (int i = 0; i < CASE_COUNT; i++)
        if (range_upper(&cases[i], wc))
            return wc + (wint_t)cases[i].distance;
    return wc;
}

int iswalpha(wint_t wc)
{
    if (wc < 0x80)
        return isalpha((int)wc);
    return wc == 0x00AA || wc == 0x00B5 || wc == 0x00BA || iswupper(wc) || iswlower(wc) ||
           (wc >= 0x0180 && wc <= 0x024F) || caseless_letter(wc);
}

int iswdigit(wint_t wc)
{
    return wc >= '0' && wc <= '9';
}

int iswxdigit(wint_t wc)
{
    return wc < 0x80 && isxdigit((int)wc);
}

int iswalnum(wint_t wc)
{
    return iswdigit(wc) || iswalpha(wc);
}

int iswblank(wint_t wc)
{
    return wc == ' ' || wc == '\t' || wc == 0x1680 || (wc >= 0x2000 && wc <= 0x2006) ||
           (wc >= 0x2008 && wc <= 0x200A) || wc == 0x205F || wc == 0x3000;
}

int iswspace(wint_t wc)
{
    // no-break spaces (U+00A0, U+2007, U+202F) are not spaces, as in musl
    return (wc < 0x80 && isspace((int)wc)) || wc == 0x85 || wc == 0x2028 || wc == 0x2029 ||
           (wc != ' ' && wc != '\t' && iswblank(wc));
}

int iswcntrl(wint_t wc)
{
    return wc < 0x20 || (wc >= 0x7F && wc < 0xA0) || wc == 0x2028 || wc == 0x2029;
}

int iswprint(wint_t wc)
{
    if (wc < 0x80)
        return isprint((int)wc);
    return wc >= 0xA0 && wc <= 0x10FFFF && !iswcntrl(wc) && !(wc >= 0xD800 && wc <= 0xDFFF) &&
           (wc & 0xFFFE) != 0xFFFE;
}

int iswgraph(wint_t wc)
{
    return iswprint(wc) && !iswspace(wc) && wc != ' ';
}

int iswpunct(wint_t wc)
{
    return iswgraph(wc) && !iswalnum(wc);
}

static const struct
{
    const char *name;
    int (*test)(wint_t);
} classes[] = {
    {"alnum", iswalnum}, {"alpha", iswalpha}, {"blank", iswblank}, {"cntrl", iswcntrl},
    {"digit", iswdigit}, {"graph", iswgraph}, {"lower", iswlower}, {"print", iswprint},
    {"punct", iswpunct}, {"space", iswspace}, {"upper", iswupper}, {"xdigit", iswxdigit},
};

#define CLASS_COUNT ((int)(sizeof(classes) / sizeof(classes[0])))

wctype_t wctype(const char *name)
{
    for (int i = 0; name && i < CLASS_COUNT; i++)
        if (strcmp(name, classes[i].name) == 0)
            return i + 1;
    return 0;
}

int iswctype(wint_t wc, wctype_t type)
{
    return type >= 1 && type <= CLASS_COUNT && classes[type - 1].test(wc);
}

wctrans_t wctrans(const char *name)
{
    if (name && strcmp(name, "tolower") == 0)
        return 1;
    if (name && strcmp(name, "toupper") == 0)
        return 2;
    return 0;
}

wint_t towctrans(wint_t wc, wctrans_t trans)
{
    return trans == 1 ? towlower(wc) : trans == 2 ? towupper(wc) : wc;
}

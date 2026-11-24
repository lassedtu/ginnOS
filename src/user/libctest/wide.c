/**
 * @file wide.c
 * @brief libctest: UTF-8 conversion, wide strings, and the classes and
 *        case of wide characters.
 */

#include "libctest.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

/** every length of UTF-8, both ways */
static void test_utf8(void)
{
    static const struct
    {
        const char *bytes;
        wchar_t wc;
    } chars[] = {{"A", 'A'}, {"\xc3\xa6", 0xE6}, {"\xe2\x82\xac", 0x20AC},
                 {"\xf0\x9f\x98\x80", 0x1F600}};
    for (int i = 0; i < 4; i++)
    {
        wchar_t wc = 0;
        size_t length = strlen(chars[i].bytes);
        CHECK(mbrtowc(&wc, chars[i].bytes, length, NULL) == length);
        CHECK(wc == chars[i].wc);
        char out[MB_LEN_MAX];
        CHECK(wcrtomb(out, chars[i].wc, NULL) == length);
        CHECK(memcmp(out, chars[i].bytes, length) == 0);
    }
    CHECK(mbrtowc(NULL, "", 1, NULL) == 0);
}

/** bytes that are not UTF-8 are refused; a character cut short asks for more */
static void test_bad_utf8(void)
{
    wchar_t wc;
    char out[MB_LEN_MAX];
    CHECK(mbrtowc(&wc, "\x80", 1, NULL) == (size_t)-1); // a lone continuation byte
    CHECK(errno == EILSEQ);
    CHECK(mbrtowc(&wc, "\xc0\x80", 2, NULL) == (size_t)-1);         // an overlong form
    CHECK(mbrtowc(&wc, "\xed\xa0\x80", 3, NULL) == (size_t)-1);     // a surrogate
    CHECK(mbrtowc(&wc, "\xf4\x90\x80\x80", 4, NULL) == (size_t)-1); // above U+10FFFF
    CHECK(mbrtowc(&wc, "\xe2\x82", 2, NULL) == (size_t)-2);
    CHECK(mbtowc(&wc, "\xe2\x82", 2) == -1);
    CHECK(wcrtomb(out, 0xD800, NULL) == (size_t)-1);
}

static void test_wide_strings(void)
{
    wchar_t wide[8];
    CHECK(mbstowcs(wide, "h\xc3\xa6j", 8) == 3);
    CHECK(wide[1] == 0xE6);
    CHECK(wcslen(wide) == 3);
    char back[8];
    CHECK(wcstombs(back, wide, sizeof(back)) == 4);
    CHECK(strcmp(back, "h\xc3\xa6j") == 0);
    CHECK(wcschr(wide, 0xE6) == wide + 1);
    CHECK(wcscmp(wide, wide) == 0);
    CHECK(wcwidth('a') == 1);
    CHECK(wcwidth(0x4E2D) == 2); // a CJK character
    CHECK(wcwidth(0x301) == 0);  // a combining accent
}

static void test_classes(void)
{
    CHECK(iswalpha('q') && iswalpha(0xE6) && iswalpha(0x3B1) && iswalpha(0x4E2D));
    CHECK(!iswalpha('3') && !iswalpha(0x20AC));
    CHECK(iswupper(0xC6) && iswlower(0xE6) && !iswupper(0xD7)); // the times sign
    CHECK(iswspace(' ') && iswspace(0x3000) && !iswspace(0xA0));
    CHECK(iswpunct(0x20AC) && iswprint(0x20AC) && !iswcntrl(0x20AC));
    CHECK(iswcntrl(0x85) && !iswprint(0x85));
    CHECK(iswctype(0xE6, wctype("alpha")) && !iswctype('1', wctype("alpha")));
    CHECK(wctype("nothing") == 0);
}

static void test_case(void)
{
    CHECK(towupper(0xE6) == 0xC6);  // ae
    CHECK(towlower(0xC5) == 0xE5);  // A with a ring
    CHECK(towupper(0x101) == 0x100); // a with a macron (upper and lower in turn)
    CHECK(towlower(0x139) == 0x13A); // L with an acute (in turn from an odd start)
    CHECK(towupper(0x3C9) == 0x3A9); // omega
    CHECK(towlower(0x416) == 0x436); // zhe
    CHECK(towlower(0x401) == 0x451); // yo
    CHECK(towupper(0xFF) == 0x178);  // y with a diaeresis
    CHECK(towupper(0xDF) == 0xDF);   // sharp s stays
    CHECK(towupper(0xF7) == 0xF7);   // the division sign is no letter
    CHECK(towctrans('a', wctrans("toupper")) == 'A');
}

void test_wide(void)
{
    test_utf8();
    test_bad_utf8();
    test_wide_strings();
    test_classes();
    test_case();
}

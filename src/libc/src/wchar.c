/**
 * @file wchar.c
 * @brief wide characters: UTF-8 to code points and back, and the wide
 *        string functions.
 */

#include "errno.h"
#include "limits.h"
#include "stdio.h"
#include "stdlib.h"
#include "wchar.h"

#define BAD  ((size_t)-1)
#define MORE ((size_t)-2)

size_t mbrtowc(wchar_t *wc, const char *s, size_t n, mbstate_t *state)
{
    (void)state;
    if (!s)
        return 0;
    if (n == 0)
        return MORE;
    const unsigned char *p = (const unsigned char *)s;
    unsigned int c = p[0];
    size_t length;
    unsigned int least; // the smallest code point of this length (no overlong forms)
    if (c < 0x80)
    {
        if (wc)
            *wc = (wchar_t)c;
        return c ? 1 : 0;
    }
    if (c >= 0xC2 && c <= 0xDF)
    {
        length = 2;
        least = 0x80;
        c &= 0x1F;
    }
    else if (c >= 0xE0 && c <= 0xEF)
    {
        length = 3;
        least = 0x800;
        c &= 0x0F;
    }
    else if (c >= 0xF0 && c <= 0xF4)
    {
        length = 4;
        least = 0x10000;
        c &= 0x07;
    }
    else
    {
        errno = EILSEQ;
        return BAD;
    }
    for (size_t i = 1; i < length; i++)
    {
        if (i >= n)
            return MORE;
        if ((p[i] & 0xC0) != 0x80)
        {
            errno = EILSEQ;
            return BAD;
        }
        c = c << 6 | (p[i] & 0x3F);
    }
    if (c < least || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
    {
        errno = EILSEQ;
        return BAD;
    }
    if (wc)
        *wc = (wchar_t)c;
    return length;
}

size_t mbrlen(const char *s, size_t n, mbstate_t *state)
{
    return mbrtowc(NULL, s, n, state);
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *state)
{
    (void)state;
    char room[MB_LEN_MAX];
    if (!s)
        s = room;
    unsigned int c = (unsigned int)wc;
    if (c < 0x80)
    {
        s[0] = (char)c;
        return 1;
    }
    if (c < 0x800)
    {
        s[0] = (char)(0xC0 | c >> 6);
        s[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c >= 0xD800 && c <= 0xDFFF)
    {
        errno = EILSEQ;
        return BAD;
    }
    if (c < 0x10000)
    {
        s[0] = (char)(0xE0 | c >> 12);
        s[1] = (char)(0x80 | (c >> 6 & 0x3F));
        s[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    if (c <= 0x10FFFF)
    {
        s[0] = (char)(0xF0 | c >> 18);
        s[1] = (char)(0x80 | (c >> 12 & 0x3F));
        s[2] = (char)(0x80 | (c >> 6 & 0x3F));
        s[3] = (char)(0x80 | (c & 0x3F));
        return 4;
    }
    errno = EILSEQ;
    return BAD;
}

int mbsinit(const mbstate_t *state)
{
    (void)state;
    return 1;
}

wint_t btowc(int c)
{
    return c >= 0 && c < 0x80 ? (wint_t)c : WEOF;
}

int wctob(wint_t wc)
{
    return wc < 0x80 ? (int)wc : EOF;
}

int mblen(const char *s, size_t n)
{
    return mbtowc(NULL, s, n);
}

int mbtowc(wchar_t *wc, const char *s, size_t n)
{
    if (!s)
        return 0; // UTF-8 has no shift states
    size_t r = mbrtowc(wc, s, n, NULL);
    if (r == MORE)
    {
        errno = EILSEQ;
        return -1;
    }
    return r == BAD ? -1 : (int)r;
}

int wctomb(char *s, wchar_t wc)
{
    if (!s)
        return 0;
    size_t r = wcrtomb(s, wc, NULL);
    return r == BAD ? -1 : (int)r;
}

size_t mbstowcs(wchar_t *dst, const char *src, size_t n)
{
    size_t count = 0;
    while (!dst || count < n)
    {
        wchar_t wc = 0;
        size_t r = mbrtowc(&wc, src, MB_LEN_MAX, NULL);
        if (r == BAD || r == MORE)
            return BAD;
        if (dst)
            dst[count] = wc;
        if (r == 0)
            return count;
        src += r;
        count++;
    }
    return count;
}

size_t wcstombs(char *dst, const wchar_t *src, size_t n)
{
    size_t count = 0;
    for (;; src++)
    {
        char bytes[MB_LEN_MAX];
        size_t r = wcrtomb(bytes, *src, NULL);
        if (r == BAD)
            return BAD;
        if (*src == 0)
        {
            if (dst && count < n)
                dst[count] = '\0';
            return count;
        }
        if (dst && count + r > n)
            return count;
        for (size_t i = 0; dst && i < r; i++)
            dst[count + i] = bytes[i];
        count += r;
    }
}

size_t wcslen(const wchar_t *s)
{
    const wchar_t *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

int wcscmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return *a < *b ? -1 : *a > *b;
}

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (; n > 0; n--, a++, b++)
    {
        if (*a != *b)
            return *a < *b ? -1 : 1;
        if (!*a)
            return 0;
    }
    return 0;
}

wchar_t *wcscpy(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while ((*d++ = *src++))
    {
    }
    return dst;
}

wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}

wchar_t *wcschr(const wchar_t *s, wchar_t c)
{
    for (;; s++)
    {
        if (*s == c)
            return (wchar_t *)s;
        if (!*s)
            return NULL;
    }
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *found = NULL;
    for (;; s++)
    {
        if (*s == c)
            found = s;
        if (!*s)
            return (wchar_t *)found;
    }
}

wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
    for (; n > 0; n--, s++)
        if (*s == c)
            return (wchar_t *)s;
    return NULL;
}

int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (; n > 0; n--, a++, b++)
        if (*a != *b)
            return *a < *b ? -1 : 1;
    return 0;
}

wchar_t *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    for (size_t i = 0; i < n; i++)
        dst[i] = src[i];
    return dst;
}

wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n)
{
    if (dst < src)
        return wmemcpy(dst, src, n);
    for (size_t i = n; i > 0; i--)
        dst[i - 1] = src[i - 1];
    return dst;
}

wchar_t *wmemset(wchar_t *dst, wchar_t c, size_t n)
{
    for (size_t i = 0; i < n; i++)
        dst[i] = c;
    return dst;
}

int wcwidth(wchar_t wc)
{
    if (wc == 0)
        return 0;
    if (wc < 0x20 || (wc >= 0x7F && wc < 0xA0))
        return -1;
    // combining marks take no column
    if ((wc >= 0x300 && wc <= 0x36F) || (wc >= 0x200B && wc <= 0x200F))
        return 0;
    // the wide scripts of East Asia take two
    if ((wc >= 0x1100 && wc <= 0x115F) || (wc >= 0x2E80 && wc <= 0xA4CF) ||
        (wc >= 0xAC00 && wc <= 0xD7A3) || (wc >= 0xF900 && wc <= 0xFAFF) ||
        (wc >= 0xFE30 && wc <= 0xFE4F) || (wc >= 0xFF00 && wc <= 0xFF60) ||
        (wc >= 0xFFE0 && wc <= 0xFFE6) || (wc >= 0x1F300 && wc <= 0x1FAFF) ||
        (wc >= 0x20000 && wc <= 0x3FFFD))
        return 2;
    return 1;
}

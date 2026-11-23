#include "string.h"
#include "stdlib.h"
#include "stdio.h"

/**
 * @file string.c
 * @brief This file contains the implementations of string manipulation functions.
 */

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && *s1 == *s2)
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst + strlen(dst);
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strchr(const char *s, int c)
{
    while (*s)
    {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s)
    {
        if (*s == (char)c)
            last = s;
        s++;
    }
    if (c == '\0')
        return (char *)s;
    return (char *)last;
}

static char *strtok_state = NULL;

char *strtok(char *str, const char *delim)
{
    if (str)
        strtok_state = str;

    if (!strtok_state || !*strtok_state)
        return NULL;

    /* skip leading delimiters */
    while (*strtok_state)
    {
        const char *d = delim;
        int is_delim = 0;
        while (*d)
        {
            if (*strtok_state == *d)
            {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (!is_delim)
            break;
        strtok_state++;
    }

    if (!*strtok_state)
        return NULL;

    char *token = strtok_state;

    /* find end of token */
    while (*strtok_state)
    {
        const char *d = delim;
        while (*d)
        {
            if (*strtok_state == *d)
            {
                *strtok_state = '\0';
                strtok_state++;
                return token;
            }
            d++;
        }
        strtok_state++;
    }

    return token;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    while (n--)
    {
        if (*a != *b)
            return *a - *b;
        a++;
        b++;
    }
    return 0;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s)
    {
        while (n--)
            *d++ = *s++;
    }
    else
    {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *end = dst + strlen(dst);
    size_t i = 0;
    for (; i < n && src[i]; i++)
        end[i] = src[i];
    end[i] = '\0';
    return dst;
}

char *strdup(const char *s)
{
    return strndup(s, strlen(s));
}

char *strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *copy = malloc(len + 1);
    if (copy)
    {
        memcpy(copy, s, len);
        copy[len] = '\0';
    }
    return copy;
}

char *strpbrk(const char *s, const char *accept)
{
    for (; *s; s++)
        if (strchr(accept, *s))
            return (char *)s;
    return NULL;
}

size_t strspn(const char *s, const char *accept)
{
    size_t n = 0;
    while (s[n] && strchr(accept, s[n]))
        n++;
    return n;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (s[n] && !strchr(reject, s[n]))
        n++;
    return n;
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    char *s = str ? str : *saveptr;
    if (!s)
        return NULL;
    s += strspn(s, delim);
    if (*s == '\0')
    {
        *saveptr = NULL;
        return NULL;
    }
    char *end = s + strcspn(s, delim);
    if (*end)
    {
        *end = '\0';
        *saveptr = end + 1;
    }
    else
    {
        *saveptr = NULL;
    }
    return s;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        int ca = lower((unsigned char)a[i]);
        int cb = lower((unsigned char)b[i]);
        if (ca != cb || ca == 0)
            return ca - cb;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    return strncasecmp(a, b, (size_t)-1);
}

int strcoll(const char *a, const char *b)
{
    return strcmp(a, b); // one locale: the C locale
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (unsigned char)c)
            return (void *)(p + i);
    return NULL;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t len = strlen(needle);
    if (len == 0)
        return (char *)haystack;
    for (; *haystack; haystack++)
        if (*haystack == *needle && strncmp(haystack, needle, len) == 0)
            return (char *)haystack;
    return NULL;
}

char *strsignal(int sig)
{
    static const char *const names[32] = {
        [1] = "Hangup", [2] = "Interrupt", [3] = "Quit", [4] = "Illegal instruction",
        [5] = "Trace/breakpoint trap", [6] = "Aborted", [7] = "Bus error",
        [8] = "Floating point exception", [9] = "Killed", [10] = "User defined signal 1",
        [11] = "Segmentation fault", [12] = "User defined signal 2", [13] = "Broken pipe",
        [14] = "Alarm clock", [15] = "Terminated", [16] = "Stack fault",
        [17] = "Child exited", [18] = "Continued", [19] = "Stopped (signal)",
        [20] = "Stopped", [21] = "Stopped (tty input)", [22] = "Stopped (tty output)",
        [23] = "Urgent I/O condition", [24] = "CPU time limit exceeded",
        [25] = "File size limit exceeded", [26] = "Virtual timer expired",
        [27] = "Profiling timer expired", [28] = "Window changed", [29] = "I/O possible",
        [30] = "Power failure", [31] = "Bad system call",
    };
    static char unknown[32];
    if (sig > 0 && sig < 32 && names[sig])
        return (char *)names[sig];
    snprintf(unknown, sizeof(unknown), "Unknown signal %d", sig);
    return unknown;
}

#include "../include/stdio.h"
#include "../include/unistd.h"
#include "../include/string.h"

/**
 * @file stdio.c
 * @brief This file contains the implementations of standard input/output functions.
 */

/* variadic argument support (compiler builtins) */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

int putchar(int c)
{
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int puts(const char *s)
{
    size_t len = strlen(s);
    write(1, s, len);
    write(1, "\n", 1);
    return (int)len + 1;
}

/* helper: print an unsigned integer in a given base (10 or 16) */
static int print_uint(unsigned int val, int base, int uppercase)
{
    char buf[12]; /* enough for 32-bit decimal */
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    int count = 0;

    if (val == 0)
    {
        putchar('0');
        return 1;
    }

    while (val > 0)
    {
        buf[i++] = digits[val % base];
        val /= base;
    }

    /* print in reverse */
    while (i > 0)
    {
        putchar(buf[--i]);
        count++;
    }

    return count;
}

/* helper: print a signed integer */
static int print_int(int val)
{
    int count = 0;

    if (val < 0)
    {
        putchar('-');
        count++;
        /* handle INT_MIN carefully */
        if (val == -2147483647 - 1)
        {
            /* -2147483648 */
            count += print_uint(2147483648u, 10, 0);
            return count;
        }
        val = -val;
    }

    count += print_uint((unsigned int)val, 10, 0);
    return count;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    int count = 0;

    while (*fmt)
    {
        if (*fmt != '%')
        {
            putchar(*fmt);
            count++;
            fmt++;
            continue;
        }

        fmt++; /* skip '%' */

        switch (*fmt)
        {
        case 'd':
        case 'i':
        {
            int val = va_arg(ap, int);
            count += print_int(val);
            break;
        }
        case 'u':
        {
            unsigned int val = va_arg(ap, unsigned int);
            count += print_uint(val, 10, 0);
            break;
        }
        case 'x':
        {
            unsigned int val = va_arg(ap, unsigned int);
            count += print_uint(val, 16, 0);
            break;
        }
        case 'X':
        {
            unsigned int val = va_arg(ap, unsigned int);
            count += print_uint(val, 16, 1);
            break;
        }
        case 's':
        {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            size_t len = strlen(s);
            write(1, s, len);
            count += (int)len;
            break;
        }
        case 'c':
        {
            int c = va_arg(ap, int);
            putchar(c);
            count++;
            break;
        }
        case '%':
            putchar('%');
            count++;
            break;
        case '\0':
            goto done;
        default:
            putchar('%');
            putchar(*fmt);
            count += 2;
            break;
        }

        fmt++;
    }

done:
    va_end(ap);
    return count;
}

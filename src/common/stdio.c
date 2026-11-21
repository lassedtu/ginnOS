#include "stdio.h"

static StdioPutCharFn g_stdio_putchar = NULL;

void stdio_set_putchar(StdioPutCharFn func)
{
    g_stdio_putchar = func;
}

void stdio_putchar(char c)
{
    if (g_stdio_putchar)
    {
        g_stdio_putchar(c);
    }
}

// length modifiers: which integer type the conversion reads
typedef enum
{
    LENGTH_INT,       // (none), hh, h: int (char and short promote to int)
    LENGTH_LONG,      // l
    LENGTH_LONG_LONG, // ll
    LENGTH_SIZE,      // z: size_t / ssize_t
} printf_length_t;

static const char lower_digits[] = "0123456789abcdef";
static const char upper_digits[] = "0123456789ABCDEF";

/**
 * compute the quotient and remainder of a 64-bit unsigned integer divided by a 32-bit unsigned integer.
 * (since standard library is not available this is implemented manually)
 */
static void div64_32(uint64_t dividend, uint32_t divisor, uint64_t *quotientOut, uint32_t *remainderOut)
{
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    int bit;

    for (bit = 63; bit >= 0; bit--)
    {
        remainder = (remainder << 1) | ((dividend >> bit) & 1ull);

        if (remainder >= divisor)
        {
            remainder -= divisor;
            quotient |= (1ull << bit);
        }
    }

    *quotientOut = quotient;
    *remainderOut = (uint32_t)remainder;
}

/**
 * write a null-terminated string through the given output backend.
 */
static void puts_to(StdioPutCharFn out, const char *str)
{
    if (!str)
    {
        str = "(null)";
    }

    while (*str)
    {
        out(*str);
        str++;
    }
}

/**
 * write an unsigned number in a radix (8, 10 or 16).
 */
static void put_unsigned(StdioPutCharFn out, uint64_t number, uint32_t radix, bool upper)
{
    const char *digits = upper ? upper_digits : lower_digits;
    char buffer[24]; // 2^64 needs 22 octal digits
    int pos = 0;

    do
    {
        uint32_t rem;
        div64_32(number, radix, &number, &rem);
        buffer[pos++] = digits[rem];
    } while (number > 0);

    while (--pos >= 0)
    {
        out(buffer[pos]);
    }
}

/**
 * read a signed integer argument of the given length, widened to 64 bits.
 */
static int64_t arg_signed(va_list *args, printf_length_t length)
{
    switch (length)
    {
    case LENGTH_LONG:
        return va_arg(*args, long);
    case LENGTH_LONG_LONG:
        return va_arg(*args, long long);
    case LENGTH_SIZE:
        return va_arg(*args, ssize_t);
    default:
        return va_arg(*args, int);
    }
}

/**
 * read an unsigned integer argument of the given length, widened to 64 bits.
 */
static uint64_t arg_unsigned(va_list *args, printf_length_t length)
{
    switch (length)
    {
    case LENGTH_LONG:
        return va_arg(*args, unsigned long);
    case LENGTH_LONG_LONG:
        return va_arg(*args, unsigned long long);
    case LENGTH_SIZE:
        return va_arg(*args, size_t);
    default:
        return va_arg(*args, unsigned int);
    }
}

void stdio_format_args(StdioPutCharFn out, const char *fmt, va_list args)
{
    // va_list may be an array type (x86_64): work on a copy we can address
    va_list ap;
    va_copy(ap, args);

    while (*fmt)
    {
        if (*fmt != '%')
        {
            out(*fmt++);
            continue;
        }
        fmt++;

        // length modifier
        printf_length_t length = LENGTH_INT;
        if (*fmt == 'h')
        {
            fmt++;
            if (*fmt == 'h')
            {
                fmt++;
            }
        }
        else if (*fmt == 'l')
        {
            fmt++;
            length = LENGTH_LONG;
            if (*fmt == 'l')
            {
                fmt++;
                length = LENGTH_LONG_LONG;
            }
        }
        else if (*fmt == 'z')
        {
            fmt++;
            length = LENGTH_SIZE;
        }

        char spec = *fmt;
        if (spec == '\0')
        {
            break; // a lone '%' at the end of the format
        }
        fmt++;

        switch (spec)
        {
        case 'd':
        case 'i':
        {
            int64_t n = arg_signed(&ap, length);
            uint64_t magnitude = (uint64_t)n;
            if (n < 0)
            {
                out('-');
                magnitude = 0 - magnitude;
            }
            put_unsigned(out, magnitude, 10, false);
            break;
        }

        case 'u':
            put_unsigned(out, arg_unsigned(&ap, length), 10, false);
            break;

        case 'x':
            put_unsigned(out, arg_unsigned(&ap, length), 16, false);
            break;

        case 'X':
            put_unsigned(out, arg_unsigned(&ap, length), 16, true);
            break;

        case 'o':
            put_unsigned(out, arg_unsigned(&ap, length), 8, false);
            break;

        case 'p':
            puts_to(out, "0x");
            put_unsigned(out, (uintptr_t)va_arg(ap, void *), 16, false);
            break;

        case 'c':
            out((char)va_arg(ap, int));
            break;

        case 's':
            puts_to(out, va_arg(ap, const char *));
            break;

        case '%':
            out('%');
            break;

        default:
            // unknown conversion: print it as written
            out('%');
            out(spec);
            break;
        }
    }

    va_end(ap);
}

void printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    stdio_format_args(stdio_putchar, fmt, args);
    va_end(args);
}

void stdio_printf_to(StdioPutCharFn out, const char *fmt, ...)
{
    if (!out)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    stdio_format_args(out, fmt, args);
    va_end(args);
}

void puts(const char *str)
{
    while (*str)
    {
        stdio_putchar(*str);
        str++;
    }
}
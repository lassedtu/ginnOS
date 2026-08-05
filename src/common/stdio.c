#include "stdio.h"

enum
{
    PRINTF_STATE_NORMAL = 0,   // normal state, not parsing a format specifier
    PRINTF_STATE_LENGTH,       // parsing length modifier (h, hh, l, ll)
    PRINTF_STATE_LENGTH_SHORT, // parsing length modifier (h)
    PRINTF_STATE_LENGTH_LONG,  // parsing length modifier (l)
    PRINTF_STATE_SPEC,         // parsing format specifier (d, u, x, s, c, etc.)
};

enum
{
    PRINTF_LENGTH_DEFAULT = 0, // default length (int or pointer)
    PRINTF_LENGTH_SHORT_SHORT, // short short length (char)
    PRINTF_LENGTH_SHORT,       // short length (short)
    PRINTF_LENGTH_LONG,        // long length (long)
    PRINTF_LENGTH_LONG_LONG,   // long long length (long long)
};

/**
 * context structure for printf state machine.
 */
typedef struct
{
    int state;  // current state of the printf parser (one of PRINTF_STATE_*)
    int length; // current length modifier (one of PRINTF_LENGTH_*)
    int radix;  // current numeric base for number formatting (e.g., 10 for decimal, 16 for hexadecimal)
    bool sign;  // flag indicating whether the number is signed (true for signed, false for unsigned)
} PrintfContext;

static const char g_HexChars[] = "0123456789abcdef"; // hexadecimal characters for number formatting

/**
 * reset the printf context to its default state.
 */
static void printf_context_reset(PrintfContext *ctx)
{
    ctx->state = PRINTF_STATE_NORMAL;
    ctx->length = PRINTF_LENGTH_DEFAULT;
    ctx->radix = 10;
    ctx->sign = false;
}

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
 * write a number to the output (VGA text buffer).
 */
static int *printf_number(int *argp, int length, bool sign, int radix);

/**
 * handle a format specifier in printf and write the corresponding output.
 */
static int *printf_handle_spec(int *argp, PrintfContext *ctx, char spec)
{
    switch (spec)
    {
    case 'c':
        puts_char((char)*argp);
        argp++;
        break;

    case 's':
        if (ctx->length == PRINTF_LENGTH_LONG || ctx->length == PRINTF_LENGTH_LONG_LONG)
        {
            puts(*(const char **)argp);
            argp += 2;
        }
        else
        {
            puts(*(const char **)argp);
            argp++;
        }
        break;

    case '%':
        puts_char('%');
        break;

    case 'd':
    case 'i':
        ctx->radix = 10;
        ctx->sign = true;
        argp = printf_number(argp, ctx->length, ctx->sign, ctx->radix);
        break;

    case 'u':
        ctx->radix = 10;
        ctx->sign = false;
        argp = printf_number(argp, ctx->length, ctx->sign, ctx->radix);
        break;

    case 'X':
    case 'x':
    case 'p':
        ctx->radix = 16;
        ctx->sign = false;
        argp = printf_number(argp, ctx->length, ctx->sign, ctx->radix);
        break;

    case 'o':
        ctx->radix = 8;
        ctx->sign = false;
        argp = printf_number(argp, ctx->length, ctx->sign, ctx->radix);
        break;

    default:
        break;
    }

    printf_context_reset(ctx);
    return argp;
}

void puts(const char *str)
{
    while (*str)
    {
        puts_char(*str);
        str++;
    }
}

void printf(const char *fmt, ...)
{
    int *argp = (int *)&fmt;
    PrintfContext ctx;

    printf_context_reset(&ctx);

    argp++;

    while (*fmt)
    {
        char ch = *fmt;
        bool advance = true;

        switch (ctx.state)
        {
        case PRINTF_STATE_NORMAL:
            if (ch == '%')
            {
                ctx.state = PRINTF_STATE_LENGTH;
            }
            else
            {
                puts_char(ch);
            }
            break;

        case PRINTF_STATE_LENGTH:
            if (ch == 'h')
            {
                ctx.length = PRINTF_LENGTH_SHORT;
                ctx.state = PRINTF_STATE_LENGTH_SHORT;
            }
            else if (ch == 'l')
            {
                ctx.length = PRINTF_LENGTH_LONG;
                ctx.state = PRINTF_STATE_LENGTH_LONG;
            }
            else
            {
                ctx.state = PRINTF_STATE_SPEC;
                advance = false;
            }
            break;

        case PRINTF_STATE_LENGTH_SHORT:
            if (ch == 'h')
            {
                ctx.length = PRINTF_LENGTH_SHORT_SHORT;
                ctx.state = PRINTF_STATE_SPEC;
            }
            else
            {
                ctx.state = PRINTF_STATE_SPEC;
                advance = false;
            }
            break;

        case PRINTF_STATE_LENGTH_LONG:
            if (ch == 'l')
            {
                ctx.length = PRINTF_LENGTH_LONG_LONG;
                ctx.state = PRINTF_STATE_SPEC;
            }
            else
            {
                ctx.state = PRINTF_STATE_SPEC;
                advance = false;
            }
            break;

        case PRINTF_STATE_SPEC:
            argp = printf_handle_spec(argp, &ctx, ch);
            break;
        }

        if (advance)
        {
            fmt++;
        }
    }
}

static int *printf_number(int *argp, int length, bool sign, int radix)
{
    char buffer[32];
    unsigned long long number = 0;
    int number_sign = 1;
    int pos = 0;

    switch (length)
    {
    case PRINTF_LENGTH_SHORT_SHORT:
    case PRINTF_LENGTH_SHORT:
    case PRINTF_LENGTH_DEFAULT:
        if (sign)
        {
            int n = *argp;
            if (n < 0)
            {
                n = -n;
                number_sign = -1;
            }
            number = (unsigned long long)n;
        }
        else
        {
            number = *(unsigned int *)argp;
        }
        argp++;
        break;

    case PRINTF_LENGTH_LONG:
        if (sign)
        {
            long int n = *(long int *)argp;
            if (n < 0)
            {
                n = -n;
                number_sign = -1;
            }
            number = (unsigned long long)n;
        }
        else
        {
            number = *(unsigned long int *)argp;
        }
        argp += 2;
        break;

    case PRINTF_LENGTH_LONG_LONG:
        if (sign)
        {
            long long int n = *(long long int *)argp;
            if (n < 0)
            {
                n = -n;
                number_sign = -1;
            }
            number = (unsigned long long)n;
        }
        else
        {
            number = *(unsigned long long int *)argp;
        }
        argp += 4;
        break;
    }

    do
    {
        uint32_t rem;
        div64_32(number, (uint32_t)radix, &number, &rem);
        buffer[pos++] = g_HexChars[rem];
    } while (number > 0);

    if (sign && number_sign < 0)
        buffer[pos++] = '-';

    while (--pos >= 0)
        puts_char(buffer[pos]);

    return argp;
}

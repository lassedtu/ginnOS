#include "stdio.h"

enum
{
    PRINTF_STATE_NORMAL = 0,
    PRINTF_STATE_LENGTH,
    PRINTF_STATE_LENGTH_SHORT,
    PRINTF_STATE_LENGTH_LONG,
    PRINTF_STATE_SPEC,
};

enum
{
    PRINTF_LENGTH_DEFAULT = 0,
    PRINTF_LENGTH_SHORT_SHORT,
    PRINTF_LENGTH_SHORT,
    PRINTF_LENGTH_LONG,
    PRINTF_LENGTH_LONG_LONG,
};

typedef struct
{
    int state;
    int length;
    int radix;
    bool sign;
} PrintfContext;

static const char g_HexChars[] = "0123456789abcdef";

static void printf_context_reset(PrintfContext *ctx)
{
    ctx->state = PRINTF_STATE_NORMAL;
    ctx->length = PRINTF_LENGTH_DEFAULT;
    ctx->radix = 10;
    ctx->sign = false;
}

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

static int *printf_number(int *argp, int length, bool sign, int radix);

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

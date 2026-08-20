#include "stdio.h"
#include "unistd.h"
#include "string.h"

/**
 * @file stdio.c
 * @brief This file contains the implementations of standard input/output functions.
 *
 * The formatting engine is factored into a generic core (format_core) that
 * accepts an output callback. printf and vsnprintf are thin wrappers around it.
 */

/**
 * output context passed to the formatting engine.
 * the emit function writes characters; the context pointer carries state.
 */
typedef struct
{
    void (*emit)(const char *str, int len, void *ctx);
    void *ctx;
} output_t;

/* stdout output callback */
static void stdout_emit(const char *str, int len, void *ctx)
{
    (void)ctx;
    write(1, str, (size_t)len);
}

/* file descriptor output callback */
static void fd_emit(const char *str, int len, void *ctx)
{
    int fd = (int)(long)ctx;
    write(fd, str, (size_t)len);
}

/* buffer output context */
typedef struct
{
    char *buf;
    size_t size; /* total buffer size */
    size_t pos;  /* current write position */
} buf_ctx_t;

/* buffer output callback: writes up to size-1 chars, always tracks count */
static void buf_emit(const char *str, int len, void *ctx)
{
    buf_ctx_t *bc = (buf_ctx_t *)ctx;

    for (int i = 0; i < len; i++)
    {
        if (bc->buf && bc->pos < bc->size - 1)
            bc->buf[bc->pos] = str[i];
        bc->pos++;
    }
}

/* ─── Generic formatting engine ───────────────────────────────────────────── */

/**
 * format an unsigned integer into buf (in reverse), return length.
 */
static int uint_to_str(char *buf, unsigned int val, int base, int uppercase)
{
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;

    if (val == 0)
    {
        buf[0] = '0';
        return 1;
    }

    while (val > 0)
    {
        buf[i++] = digits[val % base];
        val /= base;
    }

    /* reverse in place */
    for (int a = 0, b = i - 1; a < b; a++, b--)
    {
        char tmp = buf[a];
        buf[a] = buf[b];
        buf[b] = tmp;
    }

    return i;
}

/**
 * core formatting engine. processes fmt + ap, emitting output via out.
 * @return total number of characters emitted.
 */
static int format_core(output_t *out, const char *fmt, va_list ap)
{
    int count = 0;
    char numbuf[12]; /* enough for 32-bit decimal with sign */

    while (*fmt)
    {
        if (*fmt != '%')
        {
            /* scan ahead for a run of non-% characters */
            const char *start = fmt;
            while (*fmt && *fmt != '%')
                fmt++;
            int len = (int)(fmt - start);
            out->emit(start, len, out->ctx);
            count += len;
            continue;
        }

        fmt++; /* skip '%' */

        switch (*fmt)
        {
        case 'd':
        case 'i':
        {
            int val = va_arg(ap, int);
            int len = 0;
            if (val < 0)
            {
                out->emit("-", 1, out->ctx);
                count++;
                if (val == -2147483647 - 1)
                {
                    /* INT_MIN: can't negate */
                    len = uint_to_str(numbuf, 2147483648u, 10, 0);
                }
                else
                {
                    len = uint_to_str(numbuf, (unsigned int)(-val), 10, 0);
                }
            }
            else
            {
                len = uint_to_str(numbuf, (unsigned int)val, 10, 0);
            }
            out->emit(numbuf, len, out->ctx);
            count += len;
            break;
        }
        case 'u':
        {
            unsigned int val = va_arg(ap, unsigned int);
            int len = uint_to_str(numbuf, val, 10, 0);
            out->emit(numbuf, len, out->ctx);
            count += len;
            break;
        }
        case 'x':
        {
            unsigned int val = va_arg(ap, unsigned int);
            int len = uint_to_str(numbuf, val, 16, 0);
            out->emit(numbuf, len, out->ctx);
            count += len;
            break;
        }
        case 'X':
        {
            unsigned int val = va_arg(ap, unsigned int);
            int len = uint_to_str(numbuf, val, 16, 1);
            out->emit(numbuf, len, out->ctx);
            count += len;
            break;
        }
        case 's':
        {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int len = (int)strlen(s);
            out->emit(s, len, out->ctx);
            count += len;
            break;
        }
        case 'c':
        {
            char c = (char)va_arg(ap, int);
            out->emit(&c, 1, out->ctx);
            count++;
            break;
        }
        case '%':
            out->emit("%", 1, out->ctx);
            count++;
            break;
        case '\0':
            goto done;
        default:
            out->emit("%", 1, out->ctx);
            out->emit(fmt, 1, out->ctx);
            count += 2;
            break;
        }

        fmt++;
    }

done:
    return count;
}

/* ─── Public API ──────────────────────────────────────────────────────────── */

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

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    output_t out = {.emit = stdout_emit, .ctx = NULL};
    int count = format_core(&out, fmt, ap);

    va_end(ap);
    return count;
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    int fd = (int)(long)stream;
    output_t out = {.emit = fd_emit, .ctx = (void *)(long)fd};
    int count = format_core(&out, fmt, ap);

    va_end(ap);
    return count;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    buf_ctx_t bc = {.buf = buf, .size = size, .pos = 0};
    output_t out = {.emit = buf_emit, .ctx = &bc};

    int count = format_core(&out, fmt, ap);

    /* null-terminate */
    if (buf && size > 0)
    {
        size_t term_pos = bc.pos < size ? bc.pos : size - 1;
        buf[term_pos] = '\0';
    }

    return count;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    int count = vsnprintf(buf, size, fmt, ap);

    va_end(ap);
    return count;
}

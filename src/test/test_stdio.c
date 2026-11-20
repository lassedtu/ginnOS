#include "ktest.h"

#include "common/stdio.h"
#include "common/string.h"

// formatter output is captured here
static char out_buf[128];
static uint32_t out_len;

static void capture(char c)
{
    if (out_len < sizeof(out_buf) - 1)
    {
        out_buf[out_len++] = c;
        out_buf[out_len] = '\0';
    }
}

/**
 * format into out_buf through the capture backend.
 */
#define FORMAT(...)                            \
    do                                         \
    {                                          \
        out_len = 0;                           \
        out_buf[0] = '\0';                     \
        stdio_printf_to(capture, __VA_ARGS__); \
    } while (0)

KTEST(printf_integers)
{
    FORMAT("%d %d %i %u", 42, -42, 0, 4000000000u);
    KEXPECT_STREQ(out_buf, "42 -42 0 4000000000");

    FORMAT("%x %X %o", 0xbeefu, 0xbeefu, 8u);
    KEXPECT_STREQ(out_buf, "beef BEEF 10");

    FORMAT("%d", (int)0x80000000);
    KEXPECT_STREQ(out_buf, "-2147483648");
}

KTEST(printf_length_modifiers)
{
    // arguments after a wide one must still line up
    FORMAT("%llu %d", 18446744073709551615ull, 7);
    KEXPECT_STREQ(out_buf, "18446744073709551615 7");

    FORMAT("%lld %llx %d", -5ll, 0x123456789abcull, 9);
    KEXPECT_STREQ(out_buf, "-5 123456789abc 9");

    FORMAT("%lu %ld %lx %d", 3000000000ul, -7l, 0xfful, 1);
    KEXPECT_STREQ(out_buf, "3000000000 -7 ff 1");

    FORMAT("%zu %zd %d", (size_t)123, (ssize_t)-4, 5);
    KEXPECT_STREQ(out_buf, "123 -4 5");

    FORMAT("%hhu %hd", 200, -3);
    KEXPECT_STREQ(out_buf, "200 -3");
}

KTEST(printf_pointers_chars_strings)
{
    FORMAT("%p", (void *)0x1000);
    KEXPECT_STREQ(out_buf, "0x1000");

    FORMAT("%c%c %s %s %%", 'o', 'k', "str", (const char *)NULL);
    KEXPECT_STREQ(out_buf, "ok str (null) %");

    // an unknown conversion is printed as written, a trailing % dropped
    FORMAT("%q 1%");
    KEXPECT_STREQ(out_buf, "%q 1");
}

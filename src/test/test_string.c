#include "ktest.h"

#include "common/memory.h"
#include "common/string.h"

KTEST(string_strlen_strcmp)
{
    KEXPECT_EQ(strlen(""), 0);
    KEXPECT_EQ(strlen("ginnOS"), 6);

    KEXPECT(strcmp("abc", "abc") == 0);
    KEXPECT(strcmp("abc", "abd") < 0);
    KEXPECT(strcmp("abd", "abc") > 0);
    KEXPECT(strcmp("ab", "abc") < 0);
    // compared as unsigned char, so high bytes sort after ascii.
    KEXPECT(strcmp("\xff", "a") > 0);

    KEXPECT(strncmp("abcX", "abcY", 3) == 0);
    KEXPECT(strncmp("abcX", "abcY", 4) < 0);
    KEXPECT(strncmp("a", "b", 0) == 0);
}

KTEST(string_strcpy_strncpy)
{
    char buf[8];
    KEXPECT_STREQ(strcpy(buf, "hi"), "hi");

    // strncpy pads the rest of the buffer with zeros.
    memset(buf, 'x', sizeof(buf));
    strncpy(buf, "ab", sizeof(buf));
    KEXPECT(buf[0] == 'a' && buf[1] == 'b');
    KEXPECT(buf[2] == 0 && buf[7] == 0);

    // and does not terminate when the source fills the count.
    memset(buf, 'x', sizeof(buf));
    strncpy(buf, "abcdef", 3);
    KEXPECT(buf[2] == 'c' && buf[3] == 'x');
}

KTEST(memory_functions)
{
    uint8_t a[16];
    uint8_t b[16];

    memset(a, 0x5A, sizeof(a));
    KEXPECT(a[0] == 0x5A && a[15] == 0x5A);

    memcpy(b, a, sizeof(a));
    KEXPECT(memcmp(a, b, sizeof(a)) == 0);
    b[7] = 0;
    KEXPECT(memcmp(a, b, sizeof(a)) > 0);

    // memmove with overlap, both directions.
    char s[] = "abcdefgh";
    memmove(s + 2, s, 4);
    KEXPECT_STREQ(s, "ababcdgh");

    char t[] = "abcdefgh";
    memmove(t, t + 2, 4);
    KEXPECT_STREQ(t, "cdefefgh");
}

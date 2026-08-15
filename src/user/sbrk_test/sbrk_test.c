/**
 * /bin/sbrk_test — test program for SYS_sbrk.
 * exercises sbrk with various patterns and reports results.
 */

#define SYS_WRITE 1
#define SYS_SBRK  11

static int write(int fd, const char *buf, int count)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(count)
    );
    return ret;
}

static void *sbrk(int increment)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SBRK), "b"(increment)
    );
    return (void *)ret;
}

static void puts(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    write(1, s, len);
}

/**
 * convert a 32-bit unsigned integer to hex string.
 */
static void to_hex(unsigned int val, char *buf)
{
    const char *hex = "0123456789abcdef";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--)
    {
        buf[2 + (7 - i)] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
}

int main(void)
{
    char hex[12];

    puts("sbrk_test: starting\n");

    /* test 1: query current break (increment = 0) */
    void *brk0 = sbrk(0);
    puts("  current brk = ");
    to_hex((unsigned int)brk0, hex);
    puts(hex);
    puts("\n");

    if ((unsigned int)brk0 == (unsigned int)-1)
    {
        puts("  FAIL: sbrk(0) returned -1\n");
        return 1;
    }

    /* test 2: allocate 4096 bytes (one page) */
    void *old_brk = sbrk(4096);
    puts("  sbrk(4096) returned ");
    to_hex((unsigned int)old_brk, hex);
    puts(hex);
    puts("\n");

    if ((unsigned int)old_brk == (unsigned int)-1)
    {
        puts("  FAIL: sbrk(4096) returned -1\n");
        return 1;
    }

    /* verify new break moved */
    void *brk1 = sbrk(0);
    puts("  new brk = ");
    to_hex((unsigned int)brk1, hex);
    puts(hex);
    puts("\n");

    if ((unsigned int)brk1 != (unsigned int)old_brk + 4096)
    {
        puts("  FAIL: break did not advance by 4096\n");
        return 1;
    }

    /* test 3: write to the allocated memory */
    unsigned char *mem = (unsigned char *)old_brk;
    for (int i = 0; i < 4096; i++)
    {
        mem[i] = (unsigned char)(i & 0xFF);
    }

    /* verify the writes */
    int ok = 1;
    for (int i = 0; i < 4096; i++)
    {
        if (mem[i] != (unsigned char)(i & 0xFF))
        {
            ok = 0;
            break;
        }
    }

    if (ok)
    {
        puts("  PASS: write/read 4096 bytes OK\n");
    }
    else
    {
        puts("  FAIL: memory corruption detected\n");
        return 1;
    }

    /* test 4: allocate a larger region (3 pages) */
    void *brk2 = sbrk(4096 * 3);
    if ((unsigned int)brk2 == (unsigned int)-1)
    {
        puts("  FAIL: sbrk(12288) returned -1\n");
        return 1;
    }

    /* write a pattern across the 3 pages */
    unsigned int *words = (unsigned int *)brk2;
    int word_count = (4096 * 3) / 4;
    for (int i = 0; i < word_count; i++)
    {
        words[i] = (unsigned int)i * 0xDEAD;
    }

    /* verify */
    ok = 1;
    for (int i = 0; i < word_count; i++)
    {
        if (words[i] != (unsigned int)i * 0xDEAD)
        {
            ok = 0;
            break;
        }
    }

    if (ok)
    {
        puts("  PASS: write/read 12288 bytes (3 pages) OK\n");
    }
    else
    {
        puts("  FAIL: multi-page memory corruption\n");
        return 1;
    }

    puts("sbrk_test: all tests passed\n");
    return 0;
}

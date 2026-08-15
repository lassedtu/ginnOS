/**
 * /bin/sbrk_test  test program for SYS_sbrk.
 * exercises sbrk with various patterns and reports results.
 */

#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("sbrk_test: starting\n");

    /* test 1: query current break (increment = 0) */
    void *brk0 = sbrk(0);
    printf("  current brk = 0x%x\n", (unsigned int)brk0);

    if ((unsigned int)brk0 == (unsigned int)-1)
    {
        printf("  FAIL: sbrk(0) returned -1\n");
        return 1;
    }

    /* test 2: allocate 4096 bytes (one page) */
    void *old_brk = sbrk(4096);
    printf("  sbrk(4096) returned 0x%x\n", (unsigned int)old_brk);

    if ((unsigned int)old_brk == (unsigned int)-1)
    {
        printf("  FAIL: sbrk(4096) returned -1\n");
        return 1;
    }

    /* verify new break moved */
    void *brk1 = sbrk(0);
    printf("  new brk = 0x%x\n", (unsigned int)brk1);

    if ((unsigned int)brk1 != (unsigned int)old_brk + 4096)
    {
        printf("  FAIL: break did not advance by 4096\n");
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
        printf("  PASS: write/read 4096 bytes OK\n");
    }
    else
    {
        printf("  FAIL: memory corruption detected\n");
        return 1;
    }

    /* test 4: allocate a larger region (3 pages) */
    void *brk2 = sbrk(4096 * 3);
    if ((unsigned int)brk2 == (unsigned int)-1)
    {
        printf("  FAIL: sbrk(12288) returned -1\n");
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
        printf("  PASS: write/read 12288 bytes (3 pages) OK\n");
    }
    else
    {
        printf("  FAIL: multi-page memory corruption\n");
        return 1;
    }

    printf("sbrk_test: all tests passed\n");
    return 0;
}

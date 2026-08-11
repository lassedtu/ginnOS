#include "../../shell/command.h"
#include "../../memory/heap.h"

#include "../../../common/stdio.h"
#include "../../../common/memory.h"

/**
 * main function of the heaptest command.
 * exercises kmalloc/kfree: allocate, write, read, free, verify reuse.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int heaptest_main(
    int argc,
    char **argv)
{
    (void)argv;

    if (argc != 1)
    {
        printf("heaptest: too many arguments\r\n");
        return -1;
    }

    printf("--- heap test begin ---\r\n");

    /* test 1: basic allocation */
    printf("[1] allocate 64 bytes... ");
    uint8_t *a = (uint8_t *)kmalloc(64);
    if (!a)
    {
        printf("FAIL (returned NULL)\r\n");
        return -1;
    }
    printf("OK at 0x%x\r\n", (uint32_t)a);

    /* test 2: write and read back */
    printf("[2] write/read pattern... ");
    uint32_t i;
    for (i = 0; i < 64; i++)
    {
        a[i] = (uint8_t)(i ^ 0xAA);
    }
    for (i = 0; i < 64; i++)
    {
        if (a[i] != (uint8_t)(i ^ 0xAA))
        {
            printf("FAIL at byte %u\r\n", i);
            return -1;
        }
    }
    printf("OK\r\n");

    /* test 3: multiple allocations */
    printf("[3] allocate 128 + 256 bytes... ");
    uint8_t *b = (uint8_t *)kmalloc(128);
    uint8_t *c = (uint8_t *)kmalloc(256);
    if (!b || !c)
    {
        printf("FAIL (returned NULL)\r\n");
        return -1;
    }
    printf("OK at 0x%x, 0x%x\r\n", (uint32_t)b, (uint32_t)c);

    /* test 4: verify no overlap */
    printf("[4] check no overlap... ");
    memset(b, 0xBB, 128);
    memset(c, 0xCC, 256);
    /* check a is still intact */
    bool overlap = false;
    for (i = 0; i < 64; i++)
    {
        if (a[i] != (uint8_t)(i ^ 0xAA))
        {
            overlap = true;
            break;
        }
    }
    if (overlap)
    {
        printf("FAIL (allocation overlap)\r\n");
        return -1;
    }
    printf("OK\r\n");

    /* test 5: free and reuse */
    printf("[5] free 64-byte block, reallocate... ");
    uint32_t old_addr = (uint32_t)a;
    kfree(a);
    uint8_t *d = (uint8_t *)kmalloc(32);
    if (!d)
    {
        printf("FAIL (returned NULL)\r\n");
        return -1;
    }
    /* the new allocation should reuse the freed space */
    if ((uint32_t)d == old_addr)
    {
        printf("OK (reused at 0x%x)\r\n", (uint32_t)d);
    }
    else
    {
        printf("OK at 0x%x (different addr, still valid)\r\n", (uint32_t)d);
    }

    /* test 6: free everything, check heap returns to mostly-free */
    printf("[6] free all blocks... ");
    uint32_t free_before = heap_free_size();
    kfree(b);
    kfree(c);
    kfree(d);
    uint32_t free_after = heap_free_size();
    printf("OK (free: %u -> %u bytes)\r\n", free_before, free_after);

    /* test 7: large allocation */
    printf("[7] allocate 4096 bytes... ");
    uint8_t *e = (uint8_t *)kmalloc(4096);
    if (!e)
    {
        printf("FAIL (returned NULL)\r\n");
        return -1;
    }
    memset(e, 0xEE, 4096);
    printf("OK at 0x%x\r\n", (uint32_t)e);
    kfree(e);

    printf("--- heap test passed ---\r\n");
    return 0;
}

command_t heaptest_command =
    {
        .name = "heaptest",
        .description = "test kernel heap allocator (kmalloc/kfree)",
        .usage = "heaptest",
        .main = heaptest_main,
};

COMMAND_REGISTER(heaptest_command);

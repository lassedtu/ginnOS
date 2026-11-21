/**
 * @file memtest.c
 * @brief Exercise the kernel's handling of user memory.
 *
 * usage: memtest <mode>
 *   heap   malloc blocks whose sizes are not page multiples and check that
 *          earlier data survives later growth of the heap and a context
 *          switch (a child process runs in between, flushing the TLB)
 *   stack  recurse about 1 MiB deep (the stack grows on demand)
 *   mmap   touch every page of an anonymous mapping (backed on demand)
 *   munmap unmap the middle of a mapping, check that the rest keeps its
 *          data, and map a new zeroed page into the hole with MAP_FIXED
 *   mprotect
 *          make a mapping read-only and write to it (the process must be
 *          killed)
 *   null   read through a NULL pointer (the process must be killed)
 *   text   write into the program's own code (the process must be killed)
 *
 * the passing modes print "memtest <mode>: ok"; the killing modes never
 * print their last line.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define HEAP_BLOCKS 8
#define HEAP_BLOCK_SIZE 5000
#define STACK_FRAME_BYTES 1024
#define STACK_DEPTH 1024
#define MMAP_BYTES (64 * 4096)

/**
 * fill blocks with a pattern, then check every byte of every block.
 */
__attribute__((noinline)) static int test_heap(void)
{
    unsigned char *blocks[HEAP_BLOCKS];

    for (int i = 0; i < HEAP_BLOCKS; i++)
    {
        blocks[i] = malloc(HEAP_BLOCK_SIZE);
        if (!blocks[i])
        {
            printf("memtest heap: malloc failed\n");
            return 1;
        }
        memset(blocks[i], 0x40 + i, HEAP_BLOCK_SIZE);
    }

    // run a child and wait for it: switching address spaces flushes the TLB,
    // so a stale translation can no longer hide a remapped page
    const char *child_argv[] = {"echo", "memtest heap: child ran", NULL};
    pid_t child = exec("/bin/echo", child_argv);
    if (child < 0)
    {
        printf("memtest heap: exec failed\n");
        return 1;
    }
    waitpid(child);

    for (int i = 0; i < HEAP_BLOCKS; i++)
    {
        for (int j = 0; j < HEAP_BLOCK_SIZE; j++)
        {
            if (blocks[i][j] != 0x40 + i)
            {
                printf("memtest heap: block %d byte %d lost\n", i, j);
                return 1;
            }
        }
    }

    printf("memtest heap: ok\n");
    return 0;
}

/**
 * use STACK_FRAME_BYTES of stack per level and fold the result back up so
 * the compiler cannot drop the frames.
 */
static int recurse(int depth)
{
    volatile char frame[STACK_FRAME_BYTES];
    frame[0] = (char)depth;
    frame[STACK_FRAME_BYTES - 1] = (char)depth;
    if (depth == 0)
    {
        return frame[0];
    }
    return recurse(depth - 1) + frame[STACK_FRAME_BYTES - 1] - frame[0];
}

static int test_stack(void)
{
    recurse(STACK_DEPTH);
    printf("memtest stack: ok\n");
    return 0;
}

static int test_mmap(void)
{
    unsigned char *mem = mmap(NULL, MMAP_BYTES, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
    {
        printf("memtest mmap: mmap failed\n");
        return 1;
    }

    for (int i = 0; i < MMAP_BYTES; i += 4096)
    {
        if (mem[i] != 0)
        {
            printf("memtest mmap: page %d not zeroed\n", i / 4096);
            return 1;
        }
        mem[i] = 1;
    }

    printf("memtest mmap: ok\n");
    return 0;
}

__attribute__((noinline)) static int test_munmap(void)
{
    unsigned char *mem = mmap(NULL, 4 * 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
    {
        printf("memtest munmap: mmap failed\n");
        return 1;
    }
    for (int page = 0; page < 4; page++)
    {
        memset(mem + page * 4096, 0x10 + page, 4096);
    }

    if (munmap(mem + 4096, 2 * 4096) != 0)
    {
        printf("memtest munmap: munmap failed\n");
        return 1;
    }
    if (mem[0] != 0x10 || mem[3 * 4096 + 4095] != 0x13)
    {
        printf("memtest munmap: data next to the hole lost\n");
        return 1;
    }

    // the hole can now take a new mapping, which starts zeroed
    unsigned char *hole = mmap(mem + 4096, 4096, PROT_READ | PROT_WRITE,
                               MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (hole != mem + 4096)
    {
        printf("memtest munmap: MAP_FIXED did not use the hole\n");
        return 1;
    }
    for (int i = 0; i < 4096; i++)
    {
        if (hole[i] != 0)
        {
            printf("memtest munmap: old data at byte %d of the new page\n", i);
            return 1;
        }
    }

    // mprotect on the page that is still unmapped must fail
    if (mprotect(mem + 2 * 4096, 4096, PROT_READ) == 0)
    {
        printf("memtest munmap: mprotect of an unmapped page succeeded\n");
        return 1;
    }

    printf("memtest munmap: ok\n");
    return 0;
}

static int test_mprotect(void)
{
    volatile unsigned char *mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
    {
        printf("memtest mprotect: mmap failed\n");
        return 1;
    }
    mem[0] = 42;

    if (mprotect((void *)mem, 4096, PROT_READ) != 0)
    {
        printf("memtest mprotect: mprotect failed\n");
        return 1;
    }
    printf("memtest mprotect: read %d, now writing\n", mem[0]);
    mem[0] = 43;
    printf("memtest mprotect: survived\n");
    return 1;
}

static int test_null(void)
{
    volatile int *null = NULL;
    printf("memtest null: reading address 0\n");
    int value = *null;
    printf("memtest null: survived, read %d\n", value);
    return 1;
}

static int test_text(void)
{
    // any function's code will do; test_heap is never inlined away because
    // main calls it too
    volatile unsigned char *code = (volatile unsigned char *)test_heap;
    printf("memtest text: writing into code\n");
    code[0] = 0xC3;
    printf("memtest text: survived\n");
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: memtest heap|stack|mmap|munmap|mprotect|null|text\n");
        return 1;
    }

    if (strcmp(argv[1], "heap") == 0)
        return test_heap();
    if (strcmp(argv[1], "stack") == 0)
        return test_stack();
    if (strcmp(argv[1], "mmap") == 0)
        return test_mmap();
    if (strcmp(argv[1], "munmap") == 0)
        return test_munmap();
    if (strcmp(argv[1], "mprotect") == 0)
        return test_mprotect();
    if (strcmp(argv[1], "null") == 0)
        return test_null();
    if (strcmp(argv[1], "text") == 0)
        return test_text();

    printf("memtest: unknown mode %s\n", argv[1]);
    return 1;
}

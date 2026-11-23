/**
 * @file malloc_test.c
 * @brief host tests of libc's malloc: random malloc, free and realloc with
 *        the heap checked after each, contents that must stay, alignment,
 *        large blocks, a heap that sbrk does not give in one piece, and
 *        speed with many small blocks.
 */

#include "hosttest.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

// the host's mmap, before malloc_test.h gives the name to the test's
static void *host_map(size_t length)
{
    void *p = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static int host_unmap(void *address, size_t length)
{
    return munmap(address, length);
}

#include "malloc_test.h"

#define ARENA_SIZE ((size_t)256 * 1024 * 1024)

static char *arena;
static size_t arena_used;
static int sbrk_gap; // the next sbrk leaves this many bytes out first

void *test_sbrk(int increment)
{
    if (sbrk_gap)
    {
        arena_used += (size_t)sbrk_gap;
        sbrk_gap = 0;
    }
    if (increment < 0 || arena_used + (size_t)increment > ARENA_SIZE)
    {
        return (void *)-1;
    }
    char *old = arena + arena_used;
    arena_used += (size_t)increment;
    return old;
}

static int mappings;

void *test_mmap(void *address, size_t length, int prot, int flags, int fd, long offset)
{
    (void)address, (void)prot, (void)flags, (void)fd, (void)offset;
    void *p = host_map(length);
    if (!p)
    {
        return MAP_FAILED;
    }
    mappings++;
    return p;
}

int test_munmap(void *address, size_t length)
{
    mappings--;
    return host_unmap(address, length);
}

static unsigned int seed = 12345;

static unsigned int next_random(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

/** a size: mostly small, sometimes up to 64 KiB, now and then over 128 KiB */
static size_t random_size(void)
{
    unsigned int kind = next_random() % 100;
    if (kind < 70)
    {
        return next_random() % 200 + 1;
    }
    if (kind < 97)
    {
        return next_random() % 65536 + 1;
    }
    return next_random() % (512 * 1024) + 128 * 1024;
}

#define SLOTS 1000

static unsigned char *slot_ptr[SLOTS];
static size_t slot_size[SLOTS];

static void fill(int slot)
{
    memset(slot_ptr[slot], (unsigned char)(slot * 7 + 1), slot_size[slot]);
}

/** true if the slot still has the bytes fill wrote (the first count) */
static bool intact(int slot, size_t count)
{
    unsigned char want = (unsigned char)(slot * 7 + 1);
    for (size_t i = 0; i < count; i++)
    {
        if (slot_ptr[slot][i] != want)
        {
            return false;
        }
    }
    return true;
}

static void random_operations(int count)
{
    int heap_bad = 0, content_bad = 0, align_bad = 0;
    for (int n = 0; n < count; n++)
    {
        int slot = (int)(next_random() % SLOTS);
        unsigned int op = next_random() % 3;
        if (!slot_ptr[slot])
        {
            slot_size[slot] = random_size();
            slot_ptr[slot] = op == 0 ? test_calloc(1, slot_size[slot]) : test_malloc(slot_size[slot]);
            if (op == 0 && !intact(slot, 0))
            {
                content_bad++;
            }
            fill(slot);
        }
        else if (op == 0)
        {
            content_bad += !intact(slot, slot_size[slot]);
            test_free(slot_ptr[slot]);
            slot_ptr[slot] = NULL;
        }
        else
        {
            size_t size = random_size();
            unsigned char *moved = test_realloc(slot_ptr[slot], size);
            slot_ptr[slot] = moved;
            size_t kept = size < slot_size[slot] ? size : slot_size[slot];
            content_bad += !intact(slot, kept);
            slot_size[slot] = size;
            fill(slot);
        }
        if (slot_ptr[slot] && ((uintptr_t)slot_ptr[slot] % 16) != 0)
        {
            align_bad++;
        }
        int rule = heap_check();
        if (rule && !heap_bad)
        {
            printf("heap rule %d broken after operation %d\n", rule, n);
        }
        heap_bad += rule != 0;
    }
    CHECK_EQ(heap_bad, 0);
    CHECK_EQ(content_bad, 0);
    CHECK_EQ(align_bad, 0);
    for (int slot = 0; slot < SLOTS; slot++)
    {
        test_free(slot_ptr[slot]);
        slot_ptr[slot] = NULL;
    }
    CHECK_EQ(heap_check(), 0);
}

static void edges(void)
{
    CHECK(test_malloc((size_t)-1) == NULL && errno == ENOMEM);
    CHECK(test_calloc((size_t)1 << 40, (size_t)1 << 40) == NULL);
    char *p = test_malloc(0);
    CHECK(p != NULL);
    test_free(p);
    test_free(NULL);
    CHECK(test_realloc(NULL, 10) != NULL);
    p = test_malloc(100);
    CHECK(test_realloc(p, 0) == NULL);

    // a large block is a mapping of its own, and free gives it back
    int before = mappings;
    p = test_malloc(1024 * 1024);
    CHECK_EQ(mappings, before + 1);
    p[1024 * 1024 - 1] = 1;
    test_free(p);
    CHECK_EQ(mappings, before);
    CHECK_EQ(heap_check(), 0);
}

/** growing one block step by step stays in place at the top of the heap */
static void grow_in_place(void)
{
    char *keep = test_malloc(32); // something before it
    char *p = test_malloc(64);
    char *first = p;
    int moved = 0;
    for (size_t size = 128; size < 100000; size += 64)
    {
        char *q = test_realloc(p, size);
        moved += q != p;
        p = q;
    }
    CHECK(p == first);
    CHECK_EQ(moved, 0);
    test_free(p);
    test_free(keep);
    CHECK_EQ(heap_check(), 0);
}

/** many small blocks: each malloc and free must not walk the heap */
static void many_small(void)
{
    enum { COUNT = 300000 };
    static void *blocks[COUNT];
    clock_t start = clock();
    for (int i = 0; i < COUNT; i++)
    {
        blocks[i] = test_malloc((size_t)(i % 7) * 8 + 24);
    }
    for (int i = 0; i < COUNT; i += 2)
    {
        test_free(blocks[i]);
    }
    for (int i = 0; i < COUNT; i += 2)
    {
        blocks[i] = test_malloc((size_t)(i % 5) * 8 + 24);
    }
    for (int i = COUNT - 1; i >= 0; i--)
    {
        test_free(blocks[i]);
    }
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("many_small: %d blocks in %.3f s\n", COUNT, seconds);
    CHECK(seconds < 1.0);
    CHECK_EQ(heap_check(), 0);
}

/** sbrk gives memory that does not follow the heap: the old end is fenced */
static void heap_with_a_gap(void)
{
    void *a = test_malloc(1000);
    sbrk_gap = 4096;
    void *big = test_malloc(100000); // needs a larger top: sbrk leaves a gap
    CHECK(big != NULL);
    memset(big, 1, 100000);
    void *b = test_malloc(2000);
    test_free(a);
    test_free(b);
    test_free(big);
    // the checker walks only the first area; random use must still work
    for (int i = 0; i < 2000; i++)
    {
        void *p = test_malloc((size_t)(i % 300) + 1);
        memset(p, 2, (size_t)(i % 300) + 1);
        if (i % 3)
        {
            test_free(p);
        }
    }
}

int main(void)
{
    arena = host_map(ARENA_SIZE);
    if (!arena)
    {
        return 1;
    }
    edges();
    grow_in_place();
    random_operations(20000);
    many_small();
    heap_with_a_gap();
    return hosttest_done("libc malloc");
}

#include "ktest.h"

#include "common/memory.h"
#include "kernel/memory/heap.h"

KTEST(heap_kmalloc_basic)
{
    heap_stats_t before = heap_get_stats();

    uint8_t *a = kmalloc(32);
    uint8_t *b = kmalloc(100);
    KEXPECT(a != NULL);
    KEXPECT(b != NULL);
    KEXPECT_EQ((uintptr_t)a % 4, 0);
    KEXPECT_EQ((uintptr_t)b % 4, 0);

    // the two blocks must not overlap: fill each and check the other survives.
    memset(a, 0xAA, 32);
    memset(b, 0xBB, 100);
    KEXPECT(a[0] == 0xAA && a[31] == 0xAA);
    KEXPECT(b[0] == 0xBB && b[99] == 0xBB);

    KEXPECT(heap_get_stats().current_usage > before.current_usage);

    kfree(a);
    kfree(b);
    KEXPECT_EQ(heap_get_stats().current_usage, before.current_usage);
}

KTEST(heap_kfree_null_is_noop)
{
    heap_stats_t before = heap_get_stats();
    kfree(NULL);
    KEXPECT_EQ(heap_get_stats().total_frees, before.total_frees);
}

KTEST(heap_kcalloc_zeroes)
{
    // dirty a block first so kcalloc can't pass by luck on fresh memory.
    uint8_t *dirty = kmalloc(256);
    KEXPECT(dirty != NULL);
    memset(dirty, 0xFF, 256);
    kfree(dirty);

    uint8_t *p = kcalloc(64, 4);
    KEXPECT(p != NULL);

    bool all_zero = true;
    for (uint32_t i = 0; i < 256; i++)
    {
        if (p[i] != 0)
            all_zero = false;
    }
    KEXPECT(all_zero);
    kfree(p);
}

KTEST(heap_krealloc_preserves_data)
{
    uint8_t *p = kmalloc(16);
    KEXPECT(p != NULL);
    for (uint32_t i = 0; i < 16; i++)
        p[i] = (uint8_t)i;

    // grow: the original 16 bytes must survive.
    p = krealloc(p, 4096);
    KEXPECT(p != NULL);
    bool kept = true;
    for (uint32_t i = 0; i < 16; i++)
    {
        if (p[i] != (uint8_t)i)
            kept = false;
    }
    KEXPECT(kept);

    // shrink: the first 8 bytes must survive.
    p = krealloc(p, 8);
    KEXPECT(p != NULL);
    kept = true;
    for (uint32_t i = 0; i < 8; i++)
    {
        if (p[i] != (uint8_t)i)
            kept = false;
    }
    KEXPECT(kept);
    kfree(p);
}

KTEST(heap_krealloc_null_and_zero)
{
    heap_stats_t before = heap_get_stats();

    // krealloc(NULL, n) behaves like kmalloc(n).
    void *p = krealloc(NULL, 64);
    KEXPECT(p != NULL);

    // krealloc(p, 0) frees p and returns NULL.
    void *q = krealloc(p, 0);
    KEXPECT(q == NULL);
    KEXPECT_EQ(heap_get_stats().current_usage, before.current_usage);
}

KTEST(heap_free_coalesces)
{
    uint32_t blocks_before = heap_block_count();

    void *a = kmalloc(64);
    void *b = kmalloc(64);
    void *c = kmalloc(64);
    KEXPECT(a && b && c);

    // free out of order so both forward and backward coalescing are needed.
    kfree(b);
    kfree(a);
    kfree(c);

    KEXPECT_EQ(heap_block_count(), blocks_before);
}

KTEST(heap_many_alloc_free_cycles)
{
    heap_stats_t before = heap_get_stats();
    uint32_t free_before = heap_free_size();

    void *ptrs[32];
    for (uint32_t round = 0; round < 50; round++)
    {
        for (uint32_t i = 0; i < 32; i++)
            ptrs[i] = kmalloc(8 + i * 24);
        for (uint32_t i = 0; i < 32; i++)
            kfree(ptrs[i]);
    }

    KEXPECT_EQ(heap_get_stats().current_usage, before.current_usage);
    KEXPECT_EQ(heap_free_size(), free_before);
}

KTEST(heap_is_consistent)
{
    KEXPECT(heap_check());
}

KTEST(heap_grows_for_large_allocation)
{
    // larger than the initial 64 KiB heap, so this forces an expansion.
    uint32_t size = 128 * 1024;
    uint8_t *p = kmalloc(size);
    KEXPECT(p != NULL);
    if (p)
    {
        // touch the whole block: it must be real, writable memory.
        memset(p, 0x3C, size);
        KEXPECT(p[0] == 0x3C && p[size / 2] == 0x3C && p[size - 1] == 0x3C);
        kfree(p);
    }
    KEXPECT(heap_total_size() >= size);
    KEXPECT(heap_check());
}

KTEST(heap_reuses_freed_expansion)
{
    uint32_t size = 128 * 1024;

    void *first = kmalloc(size);
    KEXPECT(first != NULL);
    kfree(first);

    // the space freed above must be reused rather than growing again.
    uint32_t total_before = heap_total_size();
    uint32_t segments_before = heap_segment_count();
    void *second = kmalloc(size);
    KEXPECT(second != NULL);
    KEXPECT_EQ(heap_total_size(), total_before);
    KEXPECT_EQ(heap_segment_count(), segments_before);
    kfree(second);
    KEXPECT(heap_check());
}

KTEST(heap_many_small_allocations_beyond_initial_size)
{
    // 256 x 1 KiB = 256 KiB of live allocations at once, far beyond the
    // initial heap, grown in steps rather than with one big request.
    static void *ptrs[256];
    heap_stats_t before = heap_get_stats();

    bool all_ok = true;
    for (uint32_t i = 0; i < 256; i++)
    {
        ptrs[i] = kmalloc(1024);
        if (!ptrs[i])
        {
            all_ok = false;
            break;
        }
        memset(ptrs[i], (int)(i & 0xFF), 1024);
    }
    KEXPECT(all_ok);
    KEXPECT(heap_check());

    // every block still holds its own pattern: no two allocations overlap.
    bool intact = true;
    for (uint32_t i = 0; i < 256 && ptrs[i]; i++)
    {
        uint8_t *b = ptrs[i];
        if (b[0] != (uint8_t)i || b[1023] != (uint8_t)i)
            intact = false;
    }
    KEXPECT(intact);

    for (uint32_t i = 0; i < 256; i++)
        kfree(ptrs[i]);

    KEXPECT_EQ(heap_get_stats().current_usage, before.current_usage);
    KEXPECT(heap_check());
}

KTEST(heap_krealloc_grows_across_segments)
{
    // grow one allocation well past the initial heap; the data must move
    // intact even when the new block lives in a different segment.
    uint8_t *p = kmalloc(64);
    KEXPECT(p != NULL);
    for (uint32_t i = 0; i < 64; i++)
        p[i] = (uint8_t)(0xA0 + i);

    p = krealloc(p, 200 * 1024);
    KEXPECT(p != NULL);
    bool kept = true;
    for (uint32_t i = 0; p && i < 64; i++)
    {
        if (p[i] != (uint8_t)(0xA0 + i))
            kept = false;
    }
    KEXPECT(kept);
    kfree(p);
    KEXPECT(heap_check());
}

#include "ktest.h"

#include "kernel/memory/mman.h"
#include "kernel/memory/page.h"
#include "kernel/memory/vma.h"

// static: the table is ~600 bytes, keep it off the boot stack
static vma_table_t table;

#define P(n) ((uintptr_t)(n) * PAGE_SIZE)

KTEST(vma_insert_keeps_order_and_rejects_overlap)
{
    vma_table_init(&table);

    KEXPECT(kerr_ok(vma_insert(&table, P(10), P(12), PROT_READ, VMA_ANON)));
    KEXPECT(kerr_ok(vma_insert(&table, P(2), P(4), PROT_READ, VMA_ELF)));
    KEXPECT(kerr_ok(vma_insert(&table, P(5), P(6), PROT_READ, VMA_HEAP)));
    KEXPECT_EQ(table.count, 3);
    KEXPECT_EQ(table.areas[0].start, P(2));
    KEXPECT_EQ(table.areas[1].start, P(5));
    KEXPECT_EQ(table.areas[2].start, P(10));

    // overlapping, empty or unaligned ranges are refused
    KEXPECT_EQ(vma_insert(&table, P(3), P(5), PROT_READ, VMA_ANON), KERR_EXIST);
    KEXPECT_EQ(vma_insert(&table, P(11), P(20), PROT_READ, VMA_ANON), KERR_EXIST);
    KEXPECT_EQ(vma_insert(&table, P(7), P(7), PROT_READ, VMA_ANON), KERR_INVAL);
    KEXPECT_EQ(vma_insert(&table, P(7) + 1, P(8), PROT_READ, VMA_ANON), KERR_INVAL);

    // touching neighbours are fine
    KEXPECT(kerr_ok(vma_insert(&table, P(4), P(5), PROT_READ, VMA_ANON)));
    KEXPECT_EQ(table.count, 4);
}

KTEST(vma_find_and_free_space)
{
    vma_table_init(&table);
    vma_insert(&table, P(2), P(4), PROT_READ, VMA_ELF);
    vma_insert(&table, P(6), P(7), PROT_READ | PROT_WRITE, VMA_HEAP);

    KEXPECT(vma_find(&table, P(1)) == NULL);
    KEXPECT(vma_find(&table, P(2)) == &table.areas[0]);
    KEXPECT(vma_find(&table, P(4) - 1) == &table.areas[0]);
    KEXPECT(vma_find(&table, P(4)) == NULL);
    KEXPECT(vma_find(&table, P(6) + 123) == &table.areas[1]);
    KEXPECT(vma_find_kind(&table, VMA_HEAP) == &table.areas[1]);
    KEXPECT(vma_find_kind(&table, VMA_STACK) == NULL);

    KEXPECT(vma_range_free(&table, P(4), P(6)));
    KEXPECT(!vma_range_free(&table, P(3), P(5)));

    // first fit: the 2-page gap at 4-6 fits 2 pages but not 3
    KEXPECT_EQ(vma_find_free(&table, P(2), P(0), P(20)), P(0));
    KEXPECT_EQ(vma_find_free(&table, P(2), P(2), P(20)), P(4));
    KEXPECT_EQ(vma_find_free(&table, P(3), P(2), P(20)), P(7));
    KEXPECT_EQ(vma_find_free(&table, P(3), P(2), P(9)), 0);
}

KTEST(vma_remove_trims_and_splits)
{
    vma_table_init(&table);
    vma_insert(&table, P(10), P(20), PROT_READ, VMA_ANON);
    vma_insert(&table, P(30), P(40), PROT_READ, VMA_ANON);

    // a hole in the middle splits the area
    KEXPECT(kerr_ok(vma_remove(&table, P(12), P(14))));
    KEXPECT_EQ(table.count, 3);
    KEXPECT_EQ(table.areas[0].end, P(12));
    KEXPECT_EQ(table.areas[1].start, P(14));
    KEXPECT_EQ(table.areas[1].end, P(20));

    // a range across two areas trims one's tail and the other's head
    KEXPECT(kerr_ok(vma_remove(&table, P(18), P(32))));
    KEXPECT_EQ(table.areas[1].end, P(18));
    KEXPECT_EQ(table.areas[2].start, P(32));

    // a range covering whole areas removes them
    KEXPECT(kerr_ok(vma_remove(&table, P(0), P(19))));
    KEXPECT_EQ(table.count, 1);
    KEXPECT_EQ(table.areas[0].start, P(32));

    // unmapped ranges are not an error
    KEXPECT(kerr_ok(vma_remove(&table, P(50), P(60))));
    KEXPECT_EQ(table.count, 1);

    // a range freed by remove is found again by the next search
    KEXPECT_EQ(vma_find_free(&table, P(5), P(10), P(100)), P(10));
}

KTEST(vma_table_full)
{
    vma_table_init(&table);
    for (uint32_t i = 0; i < VMA_MAX; i++)
        KEXPECT(kerr_ok(vma_insert(&table, P(2 * i), P(2 * i + 1), PROT_READ, VMA_ANON)));
    KEXPECT_EQ(vma_insert(&table, P(1000), P(1001), PROT_READ, VMA_ANON), KERR_NOSPC);

    // a split needs a free slot
    vma_table_init(&table);
    for (uint32_t i = 0; i < VMA_MAX; i++)
        vma_insert(&table, P(10 * i), P(10 * i + 5), PROT_READ, VMA_ANON);
    KEXPECT_EQ(vma_remove(&table, P(1), P(2)), KERR_NOSPC);
}

KTEST(vma_protect_splits_at_edges)
{
    vma_table_init(&table);
    vma_insert(&table, P(10), P(20), PROT_READ | PROT_WRITE, VMA_ANON);
    vma_insert(&table, P(20), P(30), PROT_READ | PROT_WRITE, VMA_HEAP);

    // a range across both areas, starting and ending inside them
    KEXPECT(kerr_ok(vma_protect(&table, P(15), P(25), PROT_READ)));
    KEXPECT_EQ(table.count, 4);
    KEXPECT_EQ(table.areas[0].end, P(15));
    KEXPECT_EQ(table.areas[0].prot, PROT_READ | PROT_WRITE);
    KEXPECT_EQ(table.areas[1].start, P(15));
    KEXPECT_EQ(table.areas[1].prot, PROT_READ);
    KEXPECT_EQ(table.areas[2].end, P(25));
    KEXPECT_EQ(table.areas[2].prot, PROT_READ);
    KEXPECT_EQ(table.areas[2].kind, VMA_HEAP);
    KEXPECT_EQ(table.areas[3].start, P(25));
    KEXPECT_EQ(table.areas[3].prot, PROT_READ | PROT_WRITE);

    // a range with a hole in it changes nothing
    vma_table_init(&table);
    vma_insert(&table, P(10), P(12), PROT_READ, VMA_ANON);
    vma_insert(&table, P(13), P(15), PROT_READ, VMA_ANON);
    KEXPECT_EQ(vma_protect(&table, P(10), P(15), PROT_NONE), KERR_NOENT);
    KEXPECT_EQ(table.count, 2);
    KEXPECT_EQ(table.areas[0].prot, PROT_READ);
}

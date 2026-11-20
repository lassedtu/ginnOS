#include "ktest.h"

#include "kernel/memory/page.h"
#include "kernel/memory/pmm.h"


KTEST(pmm_alloc_free_single)
{
    uint32_t free_before = pmm_free_count();

    void *page = pmm_alloc_page();
    KEXPECT(page != NULL);
    KEXPECT_EQ((uint32_t)page % PAGE_SIZE, 0);
    KEXPECT_EQ(pmm_free_count(), free_before - 1);
    KEXPECT(pmm_owns((uint32_t)page));

    pmm_free_page(page);
    KEXPECT_EQ(pmm_free_count(), free_before);
}

KTEST(pmm_pages_are_distinct)
{
    void *a = pmm_alloc_page();
    void *b = pmm_alloc_page();
    KEXPECT(a != NULL && b != NULL);
    KEXPECT(a != b);
    pmm_free_page(a);
    pmm_free_page(b);
}

KTEST(pmm_bulk_alloc_free)
{
    uint32_t free_before = pmm_free_count();
    void *pages[64];

    for (uint32_t i = 0; i < 64; i++)
        pages[i] = pmm_alloc_page();

    bool all_ok = true;
    for (uint32_t i = 0; i < 64; i++)
    {
        if (!pages[i])
            all_ok = false;
    }
    KEXPECT(all_ok);
    KEXPECT_EQ(pmm_free_count(), free_before - 64);

    for (uint32_t i = 0; i < 64; i++)
        pmm_free_page(pages[i]);
    KEXPECT_EQ(pmm_free_count(), free_before);
}

KTEST(pmm_does_not_own_device_memory)
{
    // QEMU stdvga's linear framebuffer lives here, far above RAM.
    KEXPECT(!pmm_owns(0xFD000000u));
    KEXPECT(pmm_free_count() <= pmm_total_count());
}

KTEST(pmm_alloc_contiguous_run)
{
    uint32_t free_before = pmm_free_count();

    uint8_t *run = pmm_alloc_contiguous(8);
    KEXPECT(run != NULL);
    KEXPECT_EQ((uint32_t)run % PAGE_SIZE, 0);
    KEXPECT_EQ(pmm_free_count(), free_before - 8);

    if (run)
    {
        // every page of the run is now used, and the run is writable memory.
        bool all_used = true;
        for (uint32_t i = 0; i < 8; i++)
        {
            if (pmm_is_page_free(((uint32_t)run / PAGE_SIZE) + i))
                all_used = false;
        }
        KEXPECT(all_used);
        run[0] = 1;
        run[8 * PAGE_SIZE - 1] = 2;
        KEXPECT(run[0] == 1 && run[8 * PAGE_SIZE - 1] == 2);

        for (uint32_t i = 0; i < 8; i++)
            pmm_free_page(run + i * PAGE_SIZE);
    }
    KEXPECT_EQ(pmm_free_count(), free_before);
}

KTEST(pmm_alloc_contiguous_rejects_impossible)
{
    uint32_t free_before = pmm_free_count();
    KEXPECT(pmm_alloc_contiguous(0) == NULL);
    KEXPECT(pmm_alloc_contiguous(pmm_total_count() + 1) == NULL);
    KEXPECT_EQ(pmm_free_count(), free_before);
}

KTEST(pmm_claim_pages_all_or_nothing)
{
    uint8_t *run = pmm_alloc_contiguous(4);
    KEXPECT(run != NULL);
    if (!run)
        return;

    uint32_t first = (uint32_t)run / PAGE_SIZE;

    // free pages 1-3 of the run, keep page 0 used
    for (uint32_t i = 1; i < 4; i++)
        pmm_free_page(run + i * PAGE_SIZE);
    uint32_t free_before = pmm_free_count();

    // a range that includes a used page claims nothing
    KEXPECT(!pmm_claim_pages(first, 4));
    KEXPECT_EQ(pmm_free_count(), free_before);
    KEXPECT(pmm_is_page_free(first + 1));

    // a range of free pages is claimed in full
    KEXPECT(pmm_claim_pages(first + 1, 3));
    KEXPECT_EQ(pmm_free_count(), free_before - 3);
    KEXPECT(!pmm_is_page_free(first + 3));

    // out of range never claims
    KEXPECT(!pmm_claim_pages(pmm_total_count(), 1));
    KEXPECT(!pmm_claim_pages(first, 0xFFFFFFFFu));

    for (uint32_t i = 0; i < 4; i++)
        pmm_free_page(run + i * PAGE_SIZE);
}

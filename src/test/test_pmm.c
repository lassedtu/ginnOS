#include "ktest.h"

#include "kernel/memory/pmm.h"

#define PAGE_BYTES 4096u

KTEST(pmm_alloc_free_single)
{
    uint32_t free_before = pmm_free_count();

    void *page = pmm_alloc_page();
    KEXPECT(page != NULL);
    KEXPECT_EQ((uint32_t)page % PAGE_BYTES, 0);
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

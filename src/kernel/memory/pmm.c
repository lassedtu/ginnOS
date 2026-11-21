#include "pmm.h"

#include "pmm_layout.h"
#include "region.h"
#include "kernel/panic.h"
#include "kernel/sync/spinlock.h"
#include "common/memory.h"
#include "common/stdio.h"

/**
 * bitmap where each bit represents a 4 KiB page frame.
 * bit = 0: page is used or unavailable.
 * bit = 1: page is free and available for allocation.
 */
static uint8_t *bitmap;

/**
 * reference count of each page frame. a frame from an allocation starts at
 * 1; a frame that is shared (copy-on-write after fork) has one reference
 * for each mapping. frames that were never allocated (reserved, or free)
 * have 0.
 */
static uint16_t *refcounts;

static uint32_t total_pages;
static uint32_t free_pages;

/* index hint for next-fit allocation (avoids O(n) scan from 0 every time) */
static uint32_t search_start;

// guards the bitmap, free_pages and search_start after pmm_init()
static spinlock_t pmm_lock;

/**
 * set the bit corresponding to the given page index in the bitmap.
 * @param page_index index of the page to set.
 */
static inline void bitmap_set(uint32_t page_index)
{
    bitmap[page_index / 8] |= (1u << (page_index % 8));
}

/**
 * clear the bit corresponding to the given page index in the bitmap.
 * @param page_index index of the page to clear.
 */
static inline void bitmap_clear(uint32_t page_index)
{
    bitmap[page_index / 8] &= ~(1u << (page_index % 8));
}

/**
 * test if the bit corresponding to the given page index in the bitmap is set.
 * @param page_index index of the page to test.
 */
static inline bool bitmap_test(uint32_t page_index)
{
    return (bitmap[page_index / 8] >> (page_index % 8)) & 1u;
}

void pmm_init(boot_info_t *boot)
{
    uint32_t i;

    bitmap = (uint8_t *)phys_to_virt(pmm_bitmap_start());
    refcounts = (uint16_t *)phys_to_virt(pmm_refcount_start());
    total_pages = pmm_total_pages();
    free_pages = 0;
    search_start = 0;
    spinlock_init(&pmm_lock);

    /* start with all pages marked as used (zeroed bitmap = all used) */
    memset(bitmap, 0, pmm_bitmap_size());
    memset(refcounts, 0, total_pages * sizeof(uint16_t));

    /* walk E820: for each usable region, mark its pages as free */
    for (i = 0; i < boot->memory_map.count && i < 32u; i++)
    {
        memory_region_t *region = &boot->memory_map.regions[i];

        /* only process usable RAM (type 1) */
        if (region->type != 1)
        {
            continue;
        }

        /* skip regions above 4 GiB */
        if (region->base >> 32)
        {
            continue;
        }

        uint32_t region_start = (uint32_t)region->base;
        uint64_t region_end_64 = region->base + region->length;
        uint32_t region_end;

        /* cap at 4 GiB */
        if (region_end_64 > 0xFFFFFFFFULL)
        {
            region_end = 0xFFFFFFFFu;
        }
        else
        {
            region_end = (uint32_t)region_end_64;
        }

        /* align start up to page boundary, end down to page boundary */
        uint32_t page_start = PAGE_COUNT(region_start);
        uint32_t page_end = region_end / PAGE_SIZE;

        /* skip page 0 (null page, never allocate) */
        if (page_start == 0)
        {
            page_start = 1;
        }

        uint32_t page;
        for (page = page_start; page < page_end && page < total_pages; page++)
        {
            uint32_t addr_start = page * PAGE_SIZE;
            uint32_t addr_end = addr_start + PAGE_SIZE;

            /* skip pages that overlap any reserved region */
            if (region_overlaps(addr_start, addr_end))
            {
                continue;
            }

            bitmap_set(page);
            free_pages++;
        }
    }
}

void *pmm_alloc_page(void)
{
    void *page = NULL;
    uint32_t flags = spin_lock_irqsave(&pmm_lock);

    /* next-fit scan starting from search_start */
    for (uint32_t i = 0; i < total_pages; i++)
    {
        uint32_t index = (search_start + i) % total_pages;

        if (bitmap_test(index))
        {
            bitmap_clear(index);
            refcounts[index] = 1;
            free_pages--;
            search_start = (index + 1) % total_pages;
            page = phys_to_virt((phys_addr_t)index * PAGE_SIZE);
            break;
        }
    }

    spin_unlock_irqrestore(&pmm_lock, flags);
    return page;
}

void *pmm_alloc_contiguous(uint32_t count)
{
    void *run = NULL;
    uint32_t flags = spin_lock_irqsave(&pmm_lock);

    if (count == 0 || count > free_pages)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return NULL;
    }

    /* first-fit scan for a run of `count` free pages. page 0 is never free,
     * so a run can't start there. O(total_pages), fine for occasional use. */
    uint32_t run_start = 0;
    uint32_t run_length = 0;

    for (uint32_t index = 1; index < total_pages; index++)
    {
        if (!bitmap_test(index))
        {
            run_length = 0;
            continue;
        }

        if (run_length == 0)
        {
            run_start = index;
        }

        run_length++;
        if (run_length == count)
        {
            for (uint32_t page = run_start; page < run_start + count; page++)
            {
                bitmap_clear(page);
                refcounts[page] = 1;
            }
            free_pages -= count;
            run = phys_to_virt((phys_addr_t)run_start * PAGE_SIZE);
            break;
        }
    }

    spin_unlock_irqrestore(&pmm_lock, flags);
    return run;
}

/**
 * convert a direct-map address to its page index. panics on an address that
 * is not page-aligned or not managed by the PMM.
 */
static uint32_t page_index_of(void *address, const char *caller)
{
    phys_addr_t addr = virt_to_phys(address);

    if (addr % PAGE_SIZE != 0)
    {
        kernel_panic(caller);
    }

    uint32_t index = (uint32_t)(addr / PAGE_SIZE);
    if (index >= total_pages)
    {
        kernel_panic(caller);
    }
    return index;
}

void pmm_free_page(void *address)
{
    uint32_t index = page_index_of(address, "pmm_free_page: bad address");
    uint32_t flags = spin_lock_irqsave(&pmm_lock);

    if (bitmap_test(index) || refcounts[index] == 0)
    {
        kernel_panic("pmm_free_page: double free detected");
    }

    refcounts[index]--;
    if (refcounts[index] == 0)
    {
        bitmap_set(index);
        free_pages++;
    }

    spin_unlock_irqrestore(&pmm_lock, flags);
}

void pmm_page_ref(void *address)
{
    uint32_t index = page_index_of(address, "pmm_page_ref: bad address");
    uint32_t flags = spin_lock_irqsave(&pmm_lock);

    if (refcounts[index] == 0 || refcounts[index] == UINT16_MAX)
    {
        kernel_panic("pmm_page_ref: frame not allocated or count overflow");
    }
    refcounts[index]++;

    spin_unlock_irqrestore(&pmm_lock, flags);
}

uint32_t pmm_page_refcount(void *address)
{
    uint32_t index = page_index_of(address, "pmm_page_refcount: bad address");
    uint32_t flags = spin_lock_irqsave(&pmm_lock);
    uint32_t count = refcounts[index];
    spin_unlock_irqrestore(&pmm_lock, flags);
    return count;
}

uint32_t pmm_free_count(void)
{
    return free_pages;
}

uint32_t pmm_total_count(void)
{
    return total_pages;
}

bool pmm_owns(phys_addr_t phys)
{
    return (phys / PAGE_SIZE) < total_pages;
}

void pmm_mark_region_used(phys_addr_t start, phys_addr_t end)
{
    uint32_t page_start = (uint32_t)(start / PAGE_SIZE);
    uint32_t page_end = (uint32_t)(end / PAGE_SIZE);
    uint32_t flags = spin_lock_irqsave(&pmm_lock);

    for (uint32_t page = page_start; page < page_end && page < total_pages; page++)
    {
        if (bitmap_test(page))
        {
            bitmap_clear(page);
            free_pages--;
        }
    }

    spin_unlock_irqrestore(&pmm_lock, flags);
}

bool pmm_is_page_free(uint32_t page_index)
{
    if (page_index >= total_pages)
    {
        return false;
    }

    return bitmap_test(page_index);
}

bool pmm_claim_pages(uint32_t first_page, uint32_t count)
{
    if (first_page >= total_pages || count > total_pages - first_page)
    {
        return false;
    }

    uint32_t flags = spin_lock_irqsave(&pmm_lock);

    for (uint32_t i = 0; i < count; i++)
    {
        if (!bitmap_test(first_page + i))
        {
            spin_unlock_irqrestore(&pmm_lock, flags);
            return false;
        }
    }

    for (uint32_t i = 0; i < count; i++)
    {
        bitmap_clear(first_page + i);
        refcounts[first_page + i] = 1;
    }
    free_pages -= count;

    spin_unlock_irqrestore(&pmm_lock, flags);
    return true;
}

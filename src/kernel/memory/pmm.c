#include "pmm.h"

#include "pmm_layout.h"
#include "region.h"
#include "kernel/panic.h"
#include "common/memory.h"
#include "common/stdio.h"

/**
 * bitmap where each bit represents a 4 KiB page frame.
 * bit = 0: page is used or unavailable.
 * bit = 1: page is free and available for allocation.
 */
static uint8_t *bitmap;
static uint32_t total_pages;
static uint32_t free_pages;

/* index hint for next-fit allocation (avoids O(n) scan from 0 every time) */
static uint32_t search_start;

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

    bitmap = (uint8_t *)pmm_bitmap_start();
    total_pages = pmm_total_pages();
    free_pages = 0;
    search_start = 0;

    /* start with all pages marked as used (zeroed bitmap = all used) */
    memset(bitmap, 0, pmm_bitmap_size());

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
        uint32_t page_start = (region_start + PAGE_SIZE - 1) / PAGE_SIZE;
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
    uint32_t i;

    /* next-fit scan starting from search_start */
    for (i = 0; i < total_pages; i++)
    {
        uint32_t index = (search_start + i) % total_pages;

        if (bitmap_test(index))
        {
            bitmap_clear(index);
            free_pages--;
            search_start = (index + 1) % total_pages;
            return (void *)(index * PAGE_SIZE);
        }
    }

    return (void *)0;
}

void pmm_free_page(void *address)
{
    uint32_t addr = (uint32_t)address;

    if (addr % PAGE_SIZE != 0)
    {
        kernel_panic("pmm_free_page: address not page-aligned");
    }

    uint32_t index = addr / PAGE_SIZE;

    if (index >= total_pages)
    {
        kernel_panic("pmm_free_page: address out of range");
    }

    if (bitmap_test(index))
    {
        kernel_panic("pmm_free_page: double free detected");
    }

    bitmap_set(index);
    free_pages++;
}

uint32_t pmm_free_count(void)
{
    return free_pages;
}

uint32_t pmm_total_count(void)
{
    return total_pages;
}

void pmm_mark_region_used(uint32_t start, uint32_t end)
{
    uint32_t page_start = start / PAGE_SIZE;
    uint32_t page_end = end / PAGE_SIZE;
    uint32_t page;

    for (page = page_start; page < page_end && page < total_pages; page++)
    {
        if (bitmap_test(page))
        {
            bitmap_clear(page);
            free_pages--;
        }
    }
}

bool pmm_is_page_free(uint32_t page_index)
{
    if (page_index >= total_pages)
    {
        return false;
    }

    return bitmap_test(page_index);
}

void pmm_mark_page_used(uint32_t page_index)
{
    if (page_index >= total_pages)
    {
        return;
    }

    if (bitmap_test(page_index))
    {
        bitmap_clear(page_index);
        free_pages--;
    }
}

#include "heap.h"

#include "pmm.h"
#include "pmm_layout.h"
#include "reservations.h"
#include "../panic.h"
#include "../../common/stdio.h"
#include "../../common/memory.h"

/**
 * pointer to the first block in the heap's linked list.
 */
static heap_block_t *heap_head;

/**
 * physical start address of the heap region.
 */
static uint32_t heap_start;

/**
 * physical end address of the heap region (exclusive).
 */
static uint32_t heap_end;

/**
 * align a value up to the nearest 4-byte boundary.
 */
static uint32_t align4(uint32_t value)
{
    return (value + 3) & ~(uint32_t)3;
}

/**
 * attempt to split a block if the remainder is large enough.
 * @param block the block to split.
 * @param size the required data size (already aligned).
 */
static void heap_split(heap_block_t *block, uint32_t size)
{
    uint32_t remaining = block->size - size - sizeof(heap_block_t);

    /* only split if the new block would have at least HEAP_MIN_BLOCK_DATA bytes */
    if (block->size >= size + sizeof(heap_block_t) + HEAP_MIN_BLOCK_DATA)
    {
        heap_block_t *new_block = (heap_block_t *)((uint8_t *)block + sizeof(heap_block_t) + size);
        new_block->size = remaining;
        new_block->free = true;
        new_block->next = block->next;

        block->size = size;
        block->next = new_block;
    }
}

/**
 * coalesce a block with its next neighbor if both are free.
 * called after kfree to reduce fragmentation.
 * @param block the block to attempt coalescing from.
 */
static void heap_coalesce(heap_block_t *block)
{
    while (block->next && block->next->free)
    {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
    }
}

void heap_init(void)
{
    /* place the heap right after the PMM bitmap (page-aligned) */
    heap_start = (pmm_bitmap_end() + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    heap_end = heap_start + (HEAP_INITIAL_PAGES * PAGE_SIZE);

    /*
     * Reserve the heap region so the PMM will never hand out these pages.
     * We do NOT allocate from the PMM because next-fit ordering gives no
     * guarantee about which pages come back first. Instead we claim the
     * physical memory directly, it sits in usable RAM right after the
     * bitmap, so it is safe to use.
     *
     * This must be called before pmm_init() would be useful for the heap,
     * but since heap_init() runs after pmm_init(), we simply mark the pages
     * as used in the bitmap retroactively by reserving. The PMM already
     * respects the region table for future allocations.
     */
    memory_reserve_heap(heap_start, heap_end);

    /* mark the heap pages as used in the PMM bitmap so they are never
     * handed out by pmm_alloc_page() */
    pmm_mark_region_used(heap_start, heap_end);

    /* initialize the heap as a single large free block */
    heap_head = (heap_block_t *)heap_start;
    heap_head->size = (heap_end - heap_start) - sizeof(heap_block_t);
    heap_head->free = true;
    heap_head->next = (void *)0;

    /* zero the data area */
    memset((uint8_t *)heap_head + sizeof(heap_block_t), 0, heap_head->size);

    printf("Heap: %u bytes at 0x%x-0x%x\r\n",
           heap_end - heap_start,
           heap_start,
           heap_end);
}

/**
 * expand the heap by allocating more pages from the PMM.
 * the new pages must be physically contiguous with the current heap end.
 * @param needed minimum number of bytes needed (used to determine page count).
 * @return true if expansion succeeded, false otherwise.
 */
static bool heap_expand(uint32_t needed)
{
    /* calculate how many pages we need, at least HEAP_EXPAND_PAGES,
     * but more if the request is large */
    uint32_t bytes_needed = needed + sizeof(heap_block_t);
    uint32_t pages_needed = (bytes_needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages_needed < HEAP_EXPAND_PAGES)
    {
        pages_needed = HEAP_EXPAND_PAGES;
    }

    /* try to allocate contiguous pages starting at heap_end */
    uint32_t expand_start = heap_end;
    uint32_t i;

    for (i = 0; i < pages_needed; i++)
    {
        uint32_t expected_addr = expand_start + (i * PAGE_SIZE);
        uint32_t page_index = expected_addr / PAGE_SIZE;

        /* check if this specific page is free in the PMM */
        if (!pmm_is_page_free(page_index))
        {
            /* can't get contiguous pages, free what we claimed so far */
            uint32_t j;
            for (j = 0; j < i; j++)
            {
                pmm_free_page((void *)(expand_start + (j * PAGE_SIZE)));
            }
            return false;
        }

        /* mark this page as used */
        pmm_mark_page_used(page_index);
    }

    /* create a new free block spanning the expanded region */
    heap_block_t *new_block = (heap_block_t *)expand_start;
    new_block->size = (pages_needed * PAGE_SIZE) - sizeof(heap_block_t);
    new_block->free = true;
    new_block->next = (void *)0;

    /* find the last block and link to the new one */
    heap_block_t *last = heap_head;
    while (last->next)
    {
        last = last->next;
    }

    /* if the last block is free, merge it with the new block */
    if (last->free)
    {
        last->size += sizeof(heap_block_t) + new_block->size;
    }
    else
    {
        last->next = new_block;
    }

    /* update heap end */
    heap_end = expand_start + (pages_needed * PAGE_SIZE);

    return true;
}

void *kmalloc(uint32_t size)
{
    heap_block_t *current;

    if (size == 0)
    {
        return (void *)0;
    }

    /* align the requested size to 4 bytes */
    size = align4(size);

    /* first-fit search */
    current = heap_head;
    while (current)
    {
        if (current->free && current->size >= size)
        {
            /* try to split the block if it's much larger than needed */
            heap_split(current, size);

            current->free = false;

            /* return pointer to the data area (just past the header) */
            return (void *)((uint8_t *)current + sizeof(heap_block_t));
        }
        current = current->next;
    }

    /* no suitable block found, try to expand the heap */
    if (heap_expand(size))
    {
        /* retry allocation after expansion */
        current = heap_head;
        while (current)
        {
            if (current->free && current->size >= size)
            {
                heap_split(current, size);
                current->free = false;
                return (void *)((uint8_t *)current + sizeof(heap_block_t));
            }
            current = current->next;
        }
    }

    /* expansion failed or still no fit */
    return (void *)0;
}

void kfree(void *ptr)
{
    heap_block_t *block;

    if (!ptr)
    {
        return;
    }

    // sanity check: pointer must be within the heap region
    if ((uint32_t)ptr < heap_start || (uint32_t)ptr >= heap_end)
    {
        kernel_panic("kfree: pointer outside heap region");
    }

    // step back to the block header
    block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));

    // detect double-free
    if (block->free)
    {
        kernel_panic("kfree: double free detected");
    }

    block->free = true;

    // coalesce forward with adjacent free blocks
    heap_coalesce(block);

    /*
     * coalesce backward: walk from head to find predecessor.
     * this is O(n) but acceptable for a simple kernel heap.
     */
    heap_block_t *prev = heap_head;
    while (prev && prev->next != block)
    {
        prev = prev->next;
    }

    if (prev && prev->free)
    {
        heap_coalesce(prev);
    }
}

uint32_t heap_start_address(void)
{
    return heap_start;
}

uint32_t heap_end_address(void)
{
    return heap_end;
}

uint32_t heap_total_size(void)
{
    return heap_end - heap_start;
}

uint32_t heap_free_size(void)
{
    uint32_t free_bytes = 0;
    heap_block_t *current = heap_head;

    while (current)
    {
        if (current->free)
        {
            free_bytes += current->size;
        }
        current = current->next;
    }

    return free_bytes;
}

uint32_t heap_block_count(void)
{
    uint32_t count = 0;
    heap_block_t *current = heap_head;

    while (current)
    {
        count++;
        current = current->next;
    }

    return count;
}

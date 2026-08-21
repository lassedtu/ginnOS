#include "heap.h"

#include "pmm.h"
#include "pmm_layout.h"
#include "reservations.h"
#include "kernel/panic.h"
#include "common/stdio.h"
#include "common/memory.h"

// pointer to the first block in the heap's linked list.
static heap_block_t *heap_head;

// physical start address of the heap region.
static uint32_t heap_start;

// physical end address of the heap region (exclusive).
static uint32_t heap_end;

// allocation statistics.
static heap_stats_t stats;

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
    if (block->size >= size + sizeof(heap_block_t) + HEAP_MIN_BLOCK_DATA)
    {
        heap_block_t *new_block = (heap_block_t *)((uint8_t *)block + sizeof(heap_block_t) + size);
        new_block->size = block->size - size - sizeof(heap_block_t);
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

#ifdef HEAP_DEBUG
/**
 * write canary values in the redzone around an allocation.
 * @param block the allocated block (header).
 * @param user_size the size the caller requested (before redzone padding).
 */
static void heap_write_canaries(heap_block_t *block, uint32_t user_size)
{
    uint8_t *data = (uint8_t *)block + sizeof(heap_block_t);
    // front canary
    *(uint32_t *)data = HEAP_CANARY;
    // back canary (placed after user_size bytes, past the front redzone)
    *(uint32_t *)(data + HEAP_REDZONE_SIZE + user_size) = HEAP_CANARY;
}

/**
 * verify canary values around an allocation.
 * panics if either canary is corrupted.
 * @param block the allocated block (header).
 * @param user_size the size the caller requested.
 */
static void heap_check_canaries(heap_block_t *block, uint32_t user_size)
{
    uint8_t *data = (uint8_t *)block + sizeof(heap_block_t);
    if (*(uint32_t *)data != HEAP_CANARY)
    {
        kernel_panic("heap: front canary corrupted (buffer underflow)");
    }
    if (*(uint32_t *)(data + HEAP_REDZONE_SIZE + user_size) != HEAP_CANARY)
    {
        kernel_panic("heap: back canary corrupted (buffer overflow)");
    }
}

/**
 * poison freed memory with a known pattern.
 * @param ptr start of the data area.
 * @param size size of the data area in bytes.
 */
static void heap_poison(void *ptr, uint32_t size)
{
    uint32_t *p = (uint32_t *)ptr;
    uint32_t words = size / sizeof(uint32_t);
    for (uint32_t i = 0; i < words; i++)
    {
        p[i] = HEAP_POISON;
    }
}
#endif

void heap_init(void)
{
    // place the heap right after the PMM bitmap (page-aligned)
    heap_start = (pmm_bitmap_end() + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    heap_end = heap_start + (HEAP_INITIAL_PAGES * PAGE_SIZE);

    memory_reserve_heap(heap_start, heap_end);
    pmm_mark_region_used(heap_start, heap_end);

    // initialize the heap as a single large free block
    heap_head = (heap_block_t *)heap_start;
    heap_head->size = (heap_end - heap_start) - sizeof(heap_block_t);
    heap_head->free = true;
    heap_head->next = NULL;

    // zero the data area
    memset((uint8_t *)heap_head + sizeof(heap_block_t), 0, heap_head->size);

    // reset statistics
    stats.total_allocs = 0;
    stats.total_frees = 0;
    stats.current_usage = 0;
    stats.peak_usage = 0;
}

/**
 * expand the heap by allocating more pages from the PMM.
 * the new pages must be physically contiguous with the current heap end.
 * @param needed minimum number of bytes needed (used to determine page count).
 * @return true if expansion succeeded, false otherwise.
 */
static bool heap_expand(uint32_t needed)
{
    uint32_t bytes_needed = needed + sizeof(heap_block_t);
    uint32_t pages_needed = (bytes_needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages_needed < HEAP_EXPAND_PAGES)
    {
        pages_needed = HEAP_EXPAND_PAGES;
    }

    uint32_t expand_start = heap_end;

    for (uint32_t i = 0; i < pages_needed; i++)
    {
        uint32_t expected_addr = expand_start + (i * PAGE_SIZE);
        uint32_t page_index = expected_addr / PAGE_SIZE;

        if (!pmm_is_page_free(page_index))
        {
            for (uint32_t j = 0; j < i; j++)
            {
                pmm_free_page((void *)(expand_start + (j * PAGE_SIZE)));
            }
            return false;
        }

        pmm_mark_page_used(page_index);
    }

    heap_block_t *new_block = (heap_block_t *)expand_start;
    new_block->size = (pages_needed * PAGE_SIZE) - sizeof(heap_block_t);
    new_block->free = true;
    new_block->next = NULL;

    heap_block_t *last = heap_head;
    while (last->next)
    {
        last = last->next;
    }

    if (last->free)
    {
        last->size += sizeof(heap_block_t) + new_block->size;
    }
    else
    {
        last->next = new_block;
    }

    heap_end = expand_start + (pages_needed * PAGE_SIZE);
    return true;
}

/**
 * update statistics after a successful allocation.
 */
static void stats_alloc(uint32_t size)
{
    stats.total_allocs++;
    stats.current_usage += size;
    if (stats.current_usage > stats.peak_usage)
    {
        stats.peak_usage = stats.current_usage;
    }
}

/**
 * update statistics after a free.
 */
static void stats_free(uint32_t size)
{
    stats.total_frees++;
    stats.current_usage -= size;
}

void *kmalloc(uint32_t size)
{
    heap_block_t *current;

    if (size == 0)
    {
        return NULL;
    }

    uint32_t alloc_size = align4(size);

#ifdef HEAP_DEBUG
    // add space for front and back redzones
    alloc_size = align4(size + 2 * HEAP_REDZONE_SIZE);
#endif

    // first-fit search
    current = heap_head;
    while (current)
    {
        if (current->free && current->size >= alloc_size)
        {
            heap_split(current, alloc_size);
            current->free = false;
            stats_alloc(current->size);

#ifdef HEAP_DEBUG
            heap_write_canaries(current, size);
            return (void *)((uint8_t *)current + sizeof(heap_block_t) + HEAP_REDZONE_SIZE);
#else
            return (void *)((uint8_t *)current + sizeof(heap_block_t));
#endif
        }
        current = current->next;
    }

    // no suitable block found, try to expand the heap
    if (heap_expand(alloc_size))
    {
        current = heap_head;
        while (current)
        {
            if (current->free && current->size >= alloc_size)
            {
                heap_split(current, alloc_size);
                current->free = false;
                stats_alloc(current->size);

#ifdef HEAP_DEBUG
                heap_write_canaries(current, size);
                return (void *)((uint8_t *)current + sizeof(heap_block_t) + HEAP_REDZONE_SIZE);
#else
                return (void *)((uint8_t *)current + sizeof(heap_block_t));
#endif
            }
            current = current->next;
        }
    }

    return NULL;
}

void *kcalloc(uint32_t count, uint32_t size)
{
    // overflow check
    uint32_t total = count * size;
    if (count != 0 && total / count != size)
    {
        return NULL;
    }

    void *ptr = kmalloc(total);
    if (ptr)
    {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *krealloc(void *ptr, uint32_t new_size)
{
    if (!ptr)
    {
        return kmalloc(new_size);
    }

    if (new_size == 0)
    {
        kfree(ptr);
        return NULL;
    }

    uint32_t alloc_size = align4(new_size);

#ifdef HEAP_DEBUG
    alloc_size = align4(new_size + 2 * HEAP_REDZONE_SIZE);
    // step back past the front redzone to get the real block header
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - HEAP_REDZONE_SIZE - sizeof(heap_block_t));
#else
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
#endif

    // if the current block is already large enough, shrink in place
    if (block->size >= alloc_size)
    {
        // try to split off excess
        uint32_t old_size = block->size;
        heap_split(block, alloc_size);
        if (block->size != old_size)
        {
            stats.current_usage -= (old_size - block->size);
        }
#ifdef HEAP_DEBUG
        heap_write_canaries(block, new_size);
#endif
        return ptr;
    }

    // try to expand in place by absorbing the next free block
    if (block->next && block->next->free)
    {
        uint32_t combined = block->size + sizeof(heap_block_t) + block->next->size;
        if (combined >= alloc_size)
        {
            uint32_t old_size = block->size;
            block->size = combined;
            block->next = block->next->next;
            heap_split(block, alloc_size);
            stats.current_usage += (block->size - old_size);
            if (stats.current_usage > stats.peak_usage)
            {
                stats.peak_usage = stats.current_usage;
            }
#ifdef HEAP_DEBUG
            heap_write_canaries(block, new_size);
            return (void *)((uint8_t *)block + sizeof(heap_block_t) + HEAP_REDZONE_SIZE);
#else
            return (void *)((uint8_t *)block + sizeof(heap_block_t));
#endif
        }
    }

    // can't expand in place, allocate new block and copy
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr)
    {
        return NULL;
    }

    // copy the old data (minimum of old and new sizes)
    uint32_t copy_size = block->size;
#ifdef HEAP_DEBUG
    copy_size -= 2 * HEAP_REDZONE_SIZE;
#endif
    if (copy_size > new_size)
    {
        copy_size = new_size;
    }
    memcpy(new_ptr, ptr, copy_size);

    kfree(ptr);
    return new_ptr;
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

#ifdef HEAP_DEBUG
    // step back past the front redzone to get the real block header
    block = (heap_block_t *)((uint8_t *)ptr - HEAP_REDZONE_SIZE - sizeof(heap_block_t));
#else
    block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
#endif

    // detect double-free
    if (block->free)
    {
        kernel_panic("kfree: double free detected");
    }

#ifdef HEAP_DEBUG
    // check canaries before freeing
    uint32_t user_size = block->size - 2 * HEAP_REDZONE_SIZE;
    heap_check_canaries(block, user_size);

    // poison the data area
    heap_poison((uint8_t *)block + sizeof(heap_block_t), block->size);
#endif

    stats_free(block->size);
    block->free = true;

    // coalesce forward with adjacent free blocks
    heap_coalesce(block);

    // coalesce backward: walk from head to find predecessor.
    // this is O(n) but acceptable for a simple kernel heap.
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

heap_stats_t heap_get_stats(void)
{
    return stats;
}

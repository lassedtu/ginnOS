#include "heap.h"

#include "pmm.h"
#include "pmm_layout.h"
#include "reservations.h"
#include "kernel/panic.h"
#include "kernel/sync/spinlock.h"
#include "common/stdio.h"
#include "common/memory.h"

// pointer to the first block in the heap's linked list.
static heap_block_t *heap_head;

/**
 * a physically contiguous region of memory owned by the heap. the block list
 * visits segments in creation order and, inside a segment, blocks in address
 * order, so the list tail is always the last block of the newest segment.
 */
typedef struct
{
    uint32_t start; // first byte of the segment
    uint32_t end;   // first byte past the segment
} heap_segment_t;

static heap_segment_t segments[HEAP_MAX_SEGMENTS];
static uint32_t segment_count;

// allocation statistics.
static heap_stats_t stats;

// guards the block list, the segments and the statistics. kmalloc and
// kfree run in preemptible syscall code, so a process switch mid-update
// could otherwise corrupt the list. not recursive: code holding it calls
// the *_locked helpers, never the public functions.
static spinlock_t heap_lock;

/**
 * align a value up to the nearest 4-byte boundary.
 */
static uint32_t align4(uint32_t value)
{
    return (value + 3) & ~(uint32_t)3;
}

/**
 * first byte past a block's data area.
 */
static uint32_t block_end(const heap_block_t *block)
{
    return (uint32_t)block + sizeof(heap_block_t) + block->size;
}

/**
 * true if `next` starts exactly where `block` ends. blocks in different
 * segments are list neighbours without being adjacent in memory, and must
 * never be merged.
 */
static bool blocks_adjacent(const heap_block_t *block, const heap_block_t *next)
{
    return next && block_end(block) == (uint32_t)next;
}

/**
 * true if the address lies inside any heap segment.
 */
static bool heap_contains(uint32_t addr)
{
    for (uint32_t i = 0; i < segment_count; i++)
    {
        if (addr >= segments[i].start && addr < segments[i].end)
        {
            return true;
        }
    }
    return false;
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
 * coalesce a block with its next neighbours while they are free and directly
 * adjacent in memory (never across a segment boundary).
 * called after kfree to reduce fragmentation.
 * @param block the block to attempt coalescing from.
 */
static void heap_coalesce(heap_block_t *block)
{
    while (block->next && block->next->free && blocks_adjacent(block, block->next))
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
    uint32_t heap_start = PAGE_ALIGN_UP(pmm_bitmap_end());
    uint32_t heap_end = heap_start + (HEAP_INITIAL_PAGES * PAGE_SIZE);

    memory_reserve_heap(heap_start, heap_end);
    pmm_mark_region_used(heap_start, heap_end);

    segments[0].start = heap_start;
    segments[0].end = heap_end;
    segment_count = 1;

    // initialize the heap as a single large free block
    heap_head = (heap_block_t *)heap_start;
    heap_head->size = (heap_end - heap_start) - sizeof(heap_block_t);
    heap_head->free = true;
    heap_head->next = NULL;

    // zero the data area
    memset((uint8_t *)heap_head + sizeof(heap_block_t), 0, heap_head->size);

    spinlock_init(&heap_lock);

    // reset statistics
    stats.total_allocs = 0;
    stats.total_frees = 0;
    stats.current_usage = 0;
    stats.peak_usage = 0;
}

/**
 * try to grow the newest segment in place by claiming the pages right after it.
 * @param pages number of pages to claim.
 * @return true if every page was free and is now owned by the heap.
 */
static bool heap_grow_in_place(uint32_t pages)
{
    heap_segment_t *newest = &segments[segment_count - 1];
    return pmm_claim_pages(newest->end / PAGE_SIZE, pages);
}

/**
 * expand the heap by at least `needed` bytes of usable space.
 * first tries to extend the newest segment in place (the pages after it are
 * free), otherwise takes a new physically contiguous run of pages from
 * anywhere in RAM as a new segment. all RAM is identity-mapped, so the new
 * pages are usable directly.
 * @param needed minimum number of bytes needed (used to determine page count).
 * @return true if expansion succeeded, false otherwise.
 */
static bool heap_expand(uint32_t needed)
{
    uint32_t bytes_needed = needed + sizeof(heap_block_t);
    uint32_t pages_needed = PAGE_COUNT(bytes_needed);
    if (pages_needed < HEAP_EXPAND_PAGES)
    {
        pages_needed = HEAP_EXPAND_PAGES;
    }
    uint32_t bytes = pages_needed * PAGE_SIZE;

    // the list tail is the last block of the newest segment.
    heap_block_t *last = heap_head;
    while (last->next)
    {
        last = last->next;
    }

    if (heap_grow_in_place(pages_needed))
    {
        heap_segment_t *newest = &segments[segment_count - 1];
        heap_block_t *new_block = (heap_block_t *)newest->end;
        newest->end += bytes;

        if (last->free)
        {
            // extend the free tail block over the new pages.
            last->size += bytes;
        }
        else
        {
            new_block->size = bytes - sizeof(heap_block_t);
            new_block->free = true;
            new_block->next = NULL;
            last->next = new_block;
        }
        return true;
    }

    if (segment_count >= HEAP_MAX_SEGMENTS)
    {
        return false;
    }

    void *run = pmm_alloc_contiguous(pages_needed);
    if (!run)
    {
        return false;
    }

    segments[segment_count].start = (uint32_t)run;
    segments[segment_count].end = (uint32_t)run + bytes;
    segment_count++;

    // a new segment is not adjacent to the old tail, so it always gets its
    // own block; heap_coalesce will never merge across the boundary.
    heap_block_t *new_block = (heap_block_t *)run;
    new_block->size = bytes - sizeof(heap_block_t);
    new_block->free = true;
    new_block->next = NULL;
    last->next = new_block;
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

/**
 * kmalloc with heap_lock already held.
 */
static void *alloc_locked(uint32_t size)
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

// defined below; realloc_locked frees the old block through it.
static void free_locked(void *ptr);

/**
 * krealloc with heap_lock already held.
 */
static void *realloc_locked(void *ptr, uint32_t new_size)
{
    if (!ptr)
    {
        return alloc_locked(new_size);
    }

    if (new_size == 0)
    {
        free_locked(ptr);
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
            // the split-off remainder is free; merge it with a free block
            // right after it so no two free neighbours are left side by side.
            heap_coalesce(block->next);
        }
#ifdef HEAP_DEBUG
        heap_write_canaries(block, new_size);
#endif
        return ptr;
    }

    // try to expand in place by absorbing the next free block (only if it is
    // directly after this one in memory, not in another segment)
    if (block->next && block->next->free && blocks_adjacent(block, block->next))
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
    void *new_ptr = alloc_locked(new_size);
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

    free_locked(ptr);
    return new_ptr;
}

/**
 * kfree with heap_lock already held.
 */
static void free_locked(void *ptr)
{
    heap_block_t *block;

    if (!ptr)
    {
        return;
    }

    // sanity check: pointer must be inside one of the heap's segments
    if (!heap_contains((uint32_t)ptr))
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

void *kmalloc(uint32_t size)
{
    uint32_t flags = spin_lock_irqsave(&heap_lock);
    void *ptr = alloc_locked(size);
    spin_unlock_irqrestore(&heap_lock, flags);
    return ptr;
}

void *krealloc(void *ptr, uint32_t new_size)
{
    uint32_t flags = spin_lock_irqsave(&heap_lock);
    void *new_ptr = realloc_locked(ptr, new_size);
    spin_unlock_irqrestore(&heap_lock, flags);
    return new_ptr;
}

void kfree(void *ptr)
{
    uint32_t flags = spin_lock_irqsave(&heap_lock);
    free_locked(ptr);
    spin_unlock_irqrestore(&heap_lock, flags);
}

uint32_t heap_total_size(void)
{
    uint32_t total = 0;

    uint32_t flags = spin_lock_irqsave(&heap_lock);
    for (uint32_t i = 0; i < segment_count; i++)
    {
        total += segments[i].end - segments[i].start;
    }
    spin_unlock_irqrestore(&heap_lock, flags);

    return total;
}

uint32_t heap_segment_count(void)
{
    return segment_count;
}

uint32_t heap_free_size(void)
{
    uint32_t free_bytes = 0;

    uint32_t flags = spin_lock_irqsave(&heap_lock);
    for (heap_block_t *current = heap_head; current; current = current->next)
    {
        if (current->free)
        {
            free_bytes += current->size;
        }
    }
    spin_unlock_irqrestore(&heap_lock, flags);

    return free_bytes;
}

uint32_t heap_block_count(void)
{
    uint32_t count = 0;

    uint32_t flags = spin_lock_irqsave(&heap_lock);
    for (heap_block_t *current = heap_head; current; current = current->next)
    {
        count++;
    }
    spin_unlock_irqrestore(&heap_lock, flags);

    return count;
}

heap_stats_t heap_get_stats(void)
{
    uint32_t flags = spin_lock_irqsave(&heap_lock);
    heap_stats_t copy = stats;
    spin_unlock_irqrestore(&heap_lock, flags);
    return copy;
}

/**
 * heap_check with heap_lock already held.
 */
static bool check_locked(void)
{
    const heap_block_t *block = heap_head;

    // the list visits segments in creation order; inside each segment the
    // blocks must tile it exactly, back to back.
    for (uint32_t s = 0; s < segment_count; s++)
    {
        if (!block || (uint32_t)block != segments[s].start)
        {
            return false;
        }

        while (block && block_end(block) < segments[s].end)
        {
            const heap_block_t *next = block->next;
            if (!next || !blocks_adjacent(block, next))
            {
                return false; // gap or overlap inside the segment
            }
            if (block->free && next->free)
            {
                return false; // two free neighbours should have merged
            }
            block = next;
        }

        if (!block || block_end(block) != segments[s].end)
        {
            return false; // last block overruns or underfills the segment
        }
        block = block->next;
    }

    return block == NULL; // no blocks outside every segment
}

bool heap_check(void)
{
    uint32_t flags = spin_lock_irqsave(&heap_lock);
    bool ok = check_locked();
    spin_unlock_irqrestore(&heap_lock, flags);
    return ok;
}

#pragma once

#include "common/stdint.h"

/**
 * initial number of pages allocated for the kernel heap.
 * 16 pages = 64 KiB.
 */
#define HEAP_INITIAL_PAGES 16

/**
 * minimum data size for a split block.
 * prevents creating uselessly small free blocks during splitting.
 */
#define HEAP_MIN_BLOCK_DATA 16

/**
 * number of pages to add when the heap needs to grow.
 * 4 pages = 16 KiB per expansion.
 */
#define HEAP_EXPAND_PAGES 4

#ifdef HEAP_DEBUG
// redzone canary value placed before and after each allocation.
#define HEAP_CANARY 0xCAFEBABEu

// poison value written over freed memory to detect use-after-free.
#define HEAP_POISON 0xDEADBEEFu

// size of the redzone (in bytes) placed at each end of an allocation.
#define HEAP_REDZONE_SIZE 4
#endif

/**
 * block header placed before each allocation.
 * the usable memory returned to the caller begins immediately after this header.
 * all allocations are 4-byte aligned.
 */
typedef struct heap_block
{
    uint32_t size;            // size of the data area in bytes (excludes header)
    bool free;                // true if this block is available for allocation
    struct heap_block *next;  // next block in the linked list (NULL if last)
} heap_block_t;

/**
 * heap allocation statistics.
 */
typedef struct
{
    uint32_t total_allocs;   // total number of kmalloc/kcalloc/krealloc calls that succeeded
    uint32_t total_frees;    // total number of kfree calls (excluding NULL)
    uint32_t current_usage;  // bytes currently allocated (data only, excludes headers)
    uint32_t peak_usage;     // highest current_usage value ever reached
} heap_stats_t;

/**
 * initialize the kernel heap.
 * allocates HEAP_INITIAL_PAGES pages from the PMM and sets up the free list.
 * must be called after pmm_init().
 */
void heap_init(void);

/**
 * allocate size bytes from the kernel heap.
 * the returned pointer is 4-byte aligned.
 * @param size number of bytes to allocate.
 * @return pointer to the allocated memory, or NULL on failure.
 */
void *kmalloc(uint32_t size);

/**
 * allocate zeroed memory for an array.
 * @param count number of elements.
 * @param size size of each element in bytes.
 * @return pointer to zeroed memory, or NULL on failure.
 */
void *kcalloc(uint32_t count, uint32_t size);

/**
 * resize a previously allocated block.
 * if possible, expands in place. otherwise allocates a new block,
 * copies the old data, and frees the original.
 * @param ptr pointer previously returned by kmalloc/kcalloc/krealloc, or NULL.
 * @param new_size new size in bytes. if 0, equivalent to kfree(ptr).
 * @return pointer to the resized memory, or NULL on failure (original unchanged).
 */
void *krealloc(void *ptr, uint32_t new_size);

/**
 * free a previously allocated block.
 * coalesces adjacent free blocks to reduce fragmentation.
 * @param ptr pointer previously returned by kmalloc(). NULL is a no-op.
 */
void kfree(void *ptr);

/**
 * return the start address of the heap region.
 */
uint32_t heap_start_address(void);

/**
 * return the current end address of the heap region (exclusive).
 */
uint32_t heap_end_address(void);

/**
 * return the total heap size in bytes.
 */
uint32_t heap_total_size(void);

/**
 * return the number of free bytes available in the heap.
 */
uint32_t heap_free_size(void);

/**
 * return the total number of blocks (free + used) in the heap.
 */
uint32_t heap_block_count(void);

/**
 * return a snapshot of the heap allocation statistics.
 */
heap_stats_t heap_get_stats(void);

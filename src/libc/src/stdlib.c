#include "../include/stdlib.h"
#include "../include/unistd.h"
#include "../include/string.h"

/**
 * @file stdlib.c
 * @brief This file contains the implementations of general utility functions.
 *
 * Includes a simple free-list memory allocator built on top of sbrk().
 */

/* Memory allocator implementation
 *
 * A first-fit free-list allocator. Each allocated block is preceded by a
 * header that records the block size and links free blocks together.
 *
 * Layout of an allocated block:
 *   [ block_header_t | user data ... ]
 *                      ^── pointer returned to caller
 *
 * The free list is a singly-linked list of freed blocks, searched first-fit.
 * Adjacent free blocks are coalesced on free() to reduce fragmentation.
 */

/** minimum alignment for returned pointers (8 bytes). */
#define ALIGN 8
#define ALIGN_UP(x) (((x) + (ALIGN - 1)) & ~(ALIGN - 1))

/** minimum sbrk request size to reduce syscall overhead. */
#define MIN_SBRK_SIZE 4096

typedef struct block_header
{
    size_t size;               /* usable size (excluding header) */
    struct block_header *next; /* next free block (only valid when free) */
    int free;                  /* 1 if block is free, 0 if allocated */
} block_header_t;

#define HEADER_SIZE (ALIGN_UP(sizeof(block_header_t)))

/** head of the free list. */
static block_header_t *free_list = NULL;

/**
 * request more memory from the kernel via sbrk.
 * @param size minimum usable bytes needed.
 * @return pointer to the new block header, or NULL on failure.
 */
static block_header_t *request_memory(size_t size)
{
    size_t total = HEADER_SIZE + size;
    if (total < MIN_SBRK_SIZE)
        total = MIN_SBRK_SIZE;

    void *ptr = sbrk((int)total);
    if (ptr == (void *)-1)
        return NULL;

    block_header_t *block = (block_header_t *)ptr;
    block->size = total - HEADER_SIZE;
    block->next = NULL;
    block->free = 0;
    return block;
}

/**
 * find a free block that can satisfy an allocation of `size` bytes.
 * uses first-fit strategy.
 * @param size aligned allocation size.
 * @return pointer to a suitable free block, or NULL if none found.
 */
static block_header_t *find_free_block(size_t size)
{
    block_header_t *current = free_list;
    while (current)
    {
        if (current->free && current->size >= size)
            return current;
        current = current->next;
    }
    return NULL;
}

/**
 * split a block if it is large enough to hold both the requested size
 * and a new free block with at least ALIGN bytes of usable space.
 * @param block the block to potentially split.
 * @param size the requested allocation size (already aligned).
 */
static void split_block(block_header_t *block, size_t size)
{
    size_t remaining = block->size - size - HEADER_SIZE;

    /* only split if the remainder is useful */
    if (block->size < size + HEADER_SIZE + ALIGN)
        return;

    block_header_t *new_block =
        (block_header_t *)((char *)block + HEADER_SIZE + size);
    new_block->size = remaining;
    new_block->next = block->next;
    new_block->free = 1;

    block->size = size;
    block->next = new_block;
}

void *malloc(size_t size)
{
    if (size == 0)
        return NULL;

    size = ALIGN_UP(size);

    /* search the free list */
    block_header_t *block = find_free_block(size);
    if (block)
    {
        split_block(block, size);
        block->free = 0;
        return (char *)block + HEADER_SIZE;
    }

    /* no suitable free block: request more memory */
    block = request_memory(size);
    if (!block)
        return NULL;

    /* append to the free list for bookkeeping */
    if (!free_list)
    {
        free_list = block;
    }
    else
    {
        block_header_t *current = free_list;
        while (current->next)
            current = current->next;
        current->next = block;
    }

    /* split off excess if sbrk gave us more than needed */
    split_block(block, size);
    block->free = 0;
    return (char *)block + HEADER_SIZE;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size)
        return NULL; /* overflow check */

    void *ptr = malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void free(void *ptr)
{
    if (!ptr)
        return;

    block_header_t *block = (block_header_t *)((char *)ptr - HEADER_SIZE);
    block->free = 1;

    /* coalesce with the next block if it is also free */
    if (block->next && block->next->free)
    {
        block->size += HEADER_SIZE + block->next->size;
        block->next = block->next->next;
    }
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);

    if (size == 0)
    {
        free(ptr);
        return NULL;
    }

    size = ALIGN_UP(size);

    block_header_t *block = (block_header_t *)((char *)ptr - HEADER_SIZE);

    /* current block is large enough */
    if (block->size >= size)
        return ptr;

    /* try to absorb the next block if it is free and adjacent */
    if (block->next && block->next->free)
    {
        /* check adjacency: next block should immediately follow this one */
        block_header_t *expected_next =
            (block_header_t *)((char *)block + HEADER_SIZE + block->size);
        if (block->next == expected_next)
        {
            size_t combined = block->size + HEADER_SIZE + block->next->size;
            if (combined >= size)
            {
                block->size = combined;
                block->next = block->next->next;
                split_block(block, size);
                return ptr;
            }
        }
    }

    /* fall back to malloc + copy + free */
    void *new_ptr = malloc(size);
    if (!new_ptr)
        return NULL;

    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}

/* ─── Other stdlib functions ─────────────────────────────────────────────── */

int atoi(const char *s)
{
    int result = 0;
    int sign = 1;

    /* skip whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;

    /* handle sign */
    if (*s == '-')
    {
        sign = -1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }

    /* convert digits */
    while (*s >= '0' && *s <= '9')
    {
        result = result * 10 + (*s - '0');
        s++;
    }

    return result * sign;
}

void exit(int status)
{
    _exit(status);
}

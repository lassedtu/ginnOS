/**
 * @file malloc.c
 * @brief malloc, free, realloc and calloc.
 *
 * the heap is one area that grows with sbrk. it is cut into chunks; each
 * chunk starts with a 16-byte header that holds the chunk's size and two
 * flags: IN_USE, and PREV_IN_USE for the chunk before it. a free chunk also
 * has its size in its last word (the footer), so free finds the chunk
 * before it and joins two free neighbours at once (boundary tags).
 *
 * free chunks are on lists by size (bins): one list for each size up to
 * 1 KiB, then four lists for each power of two. a bitmap tells which lists
 * have chunks, so malloc finds a chunk without walking the heap. the last
 * chunk of the heap (top) is free and on no list: malloc cuts from it when
 * no list has a chunk, and sbrk makes it larger.
 *
 * a request of MAP_THRESHOLD bytes or more gets its own mapping, which
 * free gives back to the kernel. if sbrk fails, a smaller request gets its
 * own mapping too.
 *
 * pointers from malloc are 16-byte aligned (the x86_64 ABI's max_align_t).
 * there is one thread, so there is no lock.
 */

#ifdef MALLOC_TEST
// the host test (src/libc/test/malloc) gives the memory, and renames the
// functions so that they do not take the place of the host's
#include "malloc_test.h"
#else
#include "errno.h"
#include "stdint.h"
#include "stdlib.h"
#include "string.h"
#include "sys/mman.h"
#include "unistd.h"
#endif

#define ALIGNMENT     16
#define HEADER        16 // the chunk header; the user's bytes start after it
#define IN_USE        ((size_t)1)
#define PREV_IN_USE   ((size_t)2)
#define MAPPED        ((size_t)4)
#define FLAGS         (IN_USE | PREV_IN_USE | MAPPED)
#define MAP_THRESHOLD ((size_t)128 * 1024)
#define PAGE          ((size_t)4096)
#define GROW_STEP     ((size_t)64 * 1024) // sbrk asks for at least this much

#define SMALL_BINS 64  // one list for each size below 1 KiB, in steps of 16
#define BIN_COUNT  128 // the other lists: four for each power of two

/** the header at the start of each chunk */
typedef struct chunk
{
    size_t size; // the whole chunk, header included, with the flags
} chunk_t;

/** the list links in a free chunk, after its header */
typedef struct links
{
    chunk_t *next;
    chunk_t *prev;
} links_t;

#define ROUND_UP(x, a) (((x) + ((a) - 1)) & ~((size_t)(a) - 1))

// the smallest chunk: the header, the links and the footer
#define MIN_CHUNK ROUND_UP(HEADER + sizeof(links_t) + sizeof(size_t), ALIGNMENT)

static chunk_t *bins[BIN_COUNT];
static uint32_t bin_map[BIN_COUNT / 32];
static chunk_t *top;     // the free chunk at the end of the heap, or NULL
static char *heap_end;   // the end of the memory sbrk gave

#ifdef MALLOC_TEST
static char *heap_start; // the first chunk, for heap_check
#endif

static size_t size_of(const chunk_t *c)
{
    return c->size & ~FLAGS;
}

static chunk_t *at(const void *base, size_t offset)
{
    return (chunk_t *)((char *)base + offset);
}

static chunk_t *next_of(const chunk_t *c)
{
    return at(c, size_of(c));
}

static links_t *links_of(chunk_t *c)
{
    return (links_t *)((char *)c + HEADER);
}

static void *user_of(chunk_t *c)
{
    return (char *)c + HEADER;
}

static chunk_t *chunk_of(void *p)
{
    return (chunk_t *)((char *)p - HEADER);
}

/** write the size of free chunk c into its last word */
static void set_footer(chunk_t *c)
{
    *(size_t *)((char *)c + size_of(c) - sizeof(size_t)) = size_of(c);
}

/** the list for a chunk of this size */
static int bin_of(size_t size)
{
    if (size < SMALL_BINS * ALIGNMENT)
    {
        return (int)(size / ALIGNMENT);
    }
    int power = 63 - __builtin_clzll((unsigned long long)size); // 10 or more
    int bin = SMALL_BINS + 4 * (power - 10) + (int)((size >> (power - 2)) & 3);
    return bin < BIN_COUNT ? bin : BIN_COUNT - 1;
}

static void bin_insert(chunk_t *c)
{
    int bin = bin_of(size_of(c));
    links_t *l = links_of(c);
    l->prev = NULL;
    l->next = bins[bin];
    if (bins[bin])
    {
        links_of(bins[bin])->prev = c;
    }
    bins[bin] = c;
    bin_map[bin / 32] |= 1u << (bin % 32);
}

static void bin_remove(chunk_t *c)
{
    int bin = bin_of(size_of(c));
    links_t *l = links_of(c);
    if (l->prev)
    {
        links_of(l->prev)->next = l->next;
    }
    else
    {
        bins[bin] = l->next;
    }
    if (l->next)
    {
        links_of(l->next)->prev = l->prev;
    }
    if (!bins[bin])
    {
        bin_map[bin / 32] &= ~(1u << (bin % 32));
    }
}

/** the first list after bin that has a chunk, or -1 */
static int next_full_bin(int bin)
{
    for (int i = bin + 1; i < BIN_COUNT; i++)
    {
        uint32_t word = bin_map[i / 32] >> (i % 32);
        if (word)
        {
            return i + __builtin_ctz(word);
        }
        i |= 31; // the rest of this word is empty
    }
    return -1;
}

/**
 * make c a free chunk on its list: the flags (IN_USE off, PREV_IN_USE as
 * it was), the footer, and PREV_IN_USE off in the chunk after it.
 */
static void make_free(chunk_t *c, size_t size)
{
    c->size = size | (c->size & PREV_IN_USE);
    set_footer(c);
    next_of(c)->size &= ~PREV_IN_USE;
    bin_insert(c);
}

/**
 * use the first need bytes of c (which is off its list) and give the rest
 * back as a free chunk when it is large enough.
 */
static void *take(chunk_t *c, size_t need)
{
    size_t size = size_of(c);
    if (size - need >= MIN_CHUNK)
    {
        c->size = need | (c->size & PREV_IN_USE) | IN_USE;
        chunk_t *rest = at(c, need);
        rest->size = PREV_IN_USE;
        make_free(rest, size - need);
    }
    else
    {
        c->size |= IN_USE;
        next_of(c)->size |= PREV_IN_USE;
    }
    return user_of(c);
}

/** a chunk with its own mapping, for a large request */
static void *map_chunk(size_t need)
{
    size_t total = ROUND_UP(need, PAGE);
    if (total < need)
    {
        return NULL;
    }
    void *memory = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (memory == MAP_FAILED)
    {
        return NULL;
    }
    chunk_t *c = memory;
    c->size = total | MAPPED | IN_USE;
    return user_of(c);
}

/**
 * end the top chunk where the heap stops (sbrk gave memory elsewhere): its
 * last 16 bytes become a fence that is always in use, so that no chunk
 * joins with what is after it, and the rest goes on a list.
 */
static void retire_top(void)
{
    size_t size = size_of(top);
    if (size >= MIN_CHUNK + HEADER)
    {
        chunk_t *fence = at(top, size - HEADER);
        fence->size = HEADER | IN_USE;
        top->size = (size - HEADER) | (top->size & PREV_IN_USE);
        set_footer(top);
        bin_insert(top);
    }
    else
    {
        top->size |= IN_USE; // too small to use: it is the fence
    }
    top = NULL;
}

/** make the top chunk at least need bytes; false if there is no memory */
static int grow_top(size_t need)
{
    size_t have = top ? size_of(top) : 0;
    size_t amount = ROUND_UP(need - have + ALIGNMENT, GROW_STEP);
    if (amount < need || amount > (size_t)INT32_MAX)
    {
        return 0;
    }
    char *memory = sbrk((int)amount);
    if (memory == (char *)-1)
    {
        return 0;
    }
    if (top && memory == heap_end)
    {
        top->size += amount;
        heap_end += amount;
        return 1;
    }

    // the first memory, or memory that does not follow the heap
    if (top)
    {
        retire_top();
    }
    char *start = (char *)ROUND_UP((uintptr_t)memory, ALIGNMENT);
    heap_end = memory + amount;
    if ((size_t)(heap_end - start) < need)
    {
        return 0;
    }
    top = (chunk_t *)start;
    top->size = (size_t)(heap_end - start) | PREV_IN_USE; // nothing before it
#ifdef MALLOC_TEST
    if (!heap_start)
    {
        heap_start = start;
    }
#endif
    return 1;
}

/** the chunk size for a request, or 0 if it is too large */
static size_t chunk_size_for(size_t bytes)
{
    if (bytes > SIZE_MAX / 2)
    {
        return 0;
    }
    size_t need = ROUND_UP(bytes + HEADER, ALIGNMENT);
    return need < MIN_CHUNK ? MIN_CHUNK : need;
}

/** a chunk of need bytes from the lists, or NULL */
static void *from_bins(size_t need)
{
    int bin = bin_of(need);
    if (bin >= SMALL_BINS)
    {
        // a list of large chunks holds a range of sizes: find one that fits
        for (chunk_t *c = bins[bin]; c; c = links_of(c)->next)
        {
            if (size_of(c) >= need)
            {
                bin_remove(c);
                return take(c, need);
            }
        }
    }
    else if (bins[bin])
    {
        chunk_t *c = bins[bin];
        bin_remove(c);
        return take(c, need);
    }
    // every chunk on a later list is large enough
    int larger = next_full_bin(bin);
    if (larger < 0)
    {
        return NULL;
    }
    chunk_t *c = bins[larger];
    bin_remove(c);
    return take(c, need);
}

/** a chunk of need bytes from the top chunk, or NULL */
static void *from_top(size_t need)
{
    if ((!top || size_of(top) < need + MIN_CHUNK) && !grow_top(need + MIN_CHUNK))
    {
        return NULL;
    }
    chunk_t *c = top;
    size_t rest = size_of(top) - need;
    c->size = need | (c->size & PREV_IN_USE) | IN_USE;
    top = at(c, need);
    top->size = rest | PREV_IN_USE;
    return user_of(c);
}

void *malloc(size_t bytes)
{
    size_t need = chunk_size_for(bytes);
    if (!need)
    {
        errno = ENOMEM;
        return NULL;
    }
    if (need >= MAP_THRESHOLD)
    {
        void *p = map_chunk(need);
        if (!p)
        {
            errno = ENOMEM;
        }
        return p;
    }
    void *p = from_bins(need);
    if (!p)
    {
        p = from_top(need);
    }
    if (!p)
    {
        p = map_chunk(need); // no more heap: a mapping of its own
    }
    if (!p)
    {
        errno = ENOMEM;
    }
    return p;
}

void free(void *p)
{
    if (!p)
    {
        return;
    }
    chunk_t *c = chunk_of(p);
    size_t size = size_of(c);
    if (c->size & MAPPED)
    {
        munmap(c, size);
        return;
    }

    // join the free chunk before it
    if (!(c->size & PREV_IN_USE))
    {
        size_t before = *(size_t *)((char *)c - sizeof(size_t));
        c = (chunk_t *)((char *)c - before);
        bin_remove(c);
        size += before;
    }
    c->size = size | (c->size & PREV_IN_USE);

    // join the chunk after it: the top chunk, or a free chunk
    chunk_t *after = at(c, size);
    if (after == top)
    {
        c->size += size_of(top);
        top = c;
        return;
    }
    if (!(after->size & IN_USE))
    {
        bin_remove(after);
        size += size_of(after);
    }
    make_free(c, size);
}

void *calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
    {
        errno = ENOMEM;
        return NULL;
    }
    void *p = malloc(count * size);
    if (p)
    {
        memset(p, 0, count * size);
    }
    return p;
}

void *realloc(void *p, size_t bytes)
{
    if (!p)
    {
        return malloc(bytes);
    }
    if (bytes == 0)
    {
        free(p);
        return NULL;
    }
    size_t need = chunk_size_for(bytes);
    if (!need)
    {
        errno = ENOMEM;
        return NULL;
    }
    chunk_t *c = chunk_of(p);
    size_t size = size_of(c);

    if (!(c->size & MAPPED))
    {
        if (need <= size)
        {
            // smaller: give back the end when it is large enough
            if (size - need >= MIN_CHUNK)
            {
                c->size = need | (c->size & (PREV_IN_USE | IN_USE));
                chunk_t *rest = at(c, need);
                rest->size = (size - need) | PREV_IN_USE | IN_USE;
                free(user_of(rest));
            }
            return p;
        }
        // larger: take from the chunk after it when that is free
        chunk_t *after = at(c, size);
        if (after == top && size + size_of(top) < need + MIN_CHUNK)
        {
            grow_top(need + MIN_CHUNK - size); // top may move; checked again below
            after = at(c, size);
        }
        if (after == top && size + size_of(top) >= need + MIN_CHUNK)
        {
            size_t rest = size + size_of(top) - need;
            c->size = need | (c->size & PREV_IN_USE) | IN_USE;
            top = at(c, need);
            top->size = rest | PREV_IN_USE;
            return p;
        }
        if (after != top && !(after->size & IN_USE) && size + size_of(after) >= need)
        {
            bin_remove(after);
            c->size = (size + size_of(after)) | (c->size & PREV_IN_USE);
            return take(c, need);
        }
    }
    else if (need <= size && need > size / 2)
    {
        return p; // a mapping that is large enough, and not much too large
    }

    void *moved = malloc(bytes);
    if (!moved)
    {
        return NULL;
    }
    size_t old_bytes = size - HEADER;
    memcpy(moved, p, old_bytes < bytes ? old_bytes : bytes);
    free(p);
    return moved;
}

#ifdef MALLOC_TEST
/**
 * check the heap from its start to the top chunk (one area of sbrk memory;
 * the test does not check this after a gap): the sizes, the flags, the
 * footers, and that every free chunk is on its list.
 * @return 0, or the number of the first rule that does not hold.
 */
int heap_check(void)
{
    if (!top)
    {
        return 0;
    }
    size_t free_seen = 0;
    int prev_used = 1;
    chunk_t *c = (chunk_t *)heap_start;
    while (c != top)
    {
        size_t size = size_of(c);
        if (size < HEADER || size % ALIGNMENT || (char *)c + size > (char *)top)
        {
            return 1; // a size that does not fit
        }
        if (!!(c->size & PREV_IN_USE) != prev_used)
        {
            return 2; // PREV_IN_USE is not the state of the chunk before
        }
        int used = (c->size & IN_USE) != 0;
        if (!used)
        {
            if (size < MIN_CHUNK || *(size_t *)((char *)c + size - sizeof(size_t)) != size)
            {
                return 3; // a free chunk without its footer
            }
            if (!prev_used)
            {
                return 4; // two free chunks side by side
            }
            int found = 0;
            for (chunk_t *f = bins[bin_of(size)]; f; f = links_of(f)->next)
            {
                found |= f == c;
            }
            if (!found)
            {
                return 5; // a free chunk that is not on its list
            }
            free_seen++;
        }
        prev_used = used;
        c = next_of(c);
    }
    if (!!(top->size & PREV_IN_USE) != prev_used || (char *)top + size_of(top) != heap_end)
    {
        return 6; // the top chunk is wrong
    }
    size_t listed = 0;
    for (int bin = 0; bin < BIN_COUNT; bin++)
    {
        int has = (bin_map[bin / 32] >> (bin % 32)) & 1;
        if (has != (bins[bin] != NULL))
        {
            return 7; // the bitmap does not match the lists
        }
        for (chunk_t *f = bins[bin]; f; f = links_of(f)->next)
        {
            if (bin_of(size_of(f)) != bin || (f->size & IN_USE))
            {
                return 8; // a chunk on the wrong list, or one in use
            }
            listed++;
        }
    }
    return listed == free_seen ? 0 : 9; // a listed chunk outside the heap
}
#endif

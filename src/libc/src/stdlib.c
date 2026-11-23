#include "stdlib.h"
#include "inttypes.h"
#include "unistd.h"
#include "string.h"
#include "stdio.h"
#include "errno.h"
#include "signal.h"
#include "sys/mman.h"
#include "sys/wait.h"
#include "ctype.h"
#include "fcntl.h"
#include "time.h"

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
#define ALIGN       8
#define ALIGN_UP(x) (((x) + (ALIGN - 1)) & ~(ALIGN - 1))

/** minimum sbrk request size to reduce syscall overhead. */
#define MIN_SBRK_SIZE 4096

typedef struct block_header
{
    size_t size;               /* usable size (excluding header) */
    struct block_header *next; /* next free block (only valid when free) */
    int free;                  /* BLOCK_USED, BLOCK_FREE or BLOCK_MAPPED */
} block_header_t;

#define BLOCK_USED   0
#define BLOCK_FREE   1
#define BLOCK_MAPPED 2 /* a large block with its own mmap; not on the list */

/* blocks of this size or more get their own mapping, which free returns */
#define MMAP_THRESHOLD (128 * 1024)
#define PAGE_SIZE 4096

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

    block_header_t *new_block = (block_header_t *)((char *)block + HEADER_SIZE + size);
    new_block->size = remaining;
    new_block->next = block->next;
    new_block->free = 1;

    block->size = size;
    block->next = new_block;
}

/**
 * a large block: its own anonymous mapping, given back to the kernel by
 * free.
 */
static void *map_block(size_t size)
{
    size_t total = (HEADER_SIZE + size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    void *memory = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (memory == MAP_FAILED)
        return NULL;

    block_header_t *block = memory;
    block->size = total - HEADER_SIZE;
    block->next = NULL;
    block->free = BLOCK_MAPPED;
    return (char *)block + HEADER_SIZE;
}

void *malloc(size_t size)
{
    if (size == 0)
        return NULL;
    if (size > (size_t)-1 - PAGE_SIZE - HEADER_SIZE)
        return NULL; /* would wrap */

    size = ALIGN_UP(size);
    if (size >= MMAP_THRESHOLD)
        return map_block(size);

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
    if (block->free == BLOCK_MAPPED)
    {
        munmap(block, block->size + HEADER_SIZE);
        return;
    }
    block->free = BLOCK_FREE;

    /* coalesce with the next block if it is free and right after this one */
    block_header_t *after = (block_header_t *)((char *)block + HEADER_SIZE + block->size);
    if (block->next && block->next->free == BLOCK_FREE && block->next == after)
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
    if (block->free != BLOCK_MAPPED && block->next && block->next->free == BLOCK_FREE)
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

// the environment, as "KEY=VALUE" strings. crt0 sets it from the envp the
// kernel passes to the program.
char **environ;

char *getenv(const char *name)
{
    size_t name_len = strlen(name);
    for (char **entry = environ; entry && *entry; entry++)
    {
        if (strncmp(*entry, name, name_len) == 0 && (*entry)[name_len] == '=')
        {
            return *entry + name_len + 1;
        }
    }
    return NULL;
}

// functions registered with atexit, run in reverse order by exit
#define ATEXIT_MAX 32
static void (*atexit_handlers[ATEXIT_MAX])(void);
static int atexit_count;

int atexit(void (*function)(void))
{
    if (atexit_count == ATEXIT_MAX)
    {
        return -1;
    }
    atexit_handlers[atexit_count++] = function;
    return 0;
}

void exit(int status)
{
    while (atexit_count > 0)
    {
        atexit_handlers[--atexit_count]();
    }
    // buffered output is written now (_exit drops it)
    fflush(NULL);
    _exit(status);
}

void abort(void)
{
    fflush(NULL);
    signal(SIGABRT, SIG_DFL);
    raise(SIGABRT);
    _exit(128 + SIGABRT); // SIGABRT blocked: end anyway
}

void __assert_fail(const char *expression, const char *file, int line, const char *function)
{
    fprintf(stderr, "%s:%d: %s: assertion failed: %s\n", file, line, function, expression);
    abort();
}

int abs(int value)
{
    return value < 0 ? -value : value;
}

long labs(long value)
{
    return value < 0 ? -value : value;
}

long long llabs(long long value)
{
    return value < 0 ? -value : value;
}

div_t div(int numerator, int denominator)
{
    div_t result = {numerator / denominator, numerator % denominator};
    return result;
}

ldiv_t ldiv(long numerator, long denominator)
{
    ldiv_t result = {numerator / denominator, numerator % denominator};
    return result;
}

static int digit_of(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return 99;
}

/**
 * the digits of a number in a base (0: from its prefix), as strtoull reads
 * them. sets *overflow if the value does not fit 64 bits.
 */
static unsigned long long parse_unsigned(const char *s, char **end, int base, int *negative,
                                         int *overflow)
{
    const char *p = s;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
        p++;
    *negative = 0;
    if (*p == '+' || *p == '-')
        *negative = *p++ == '-';

    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        digit_of(p[2]) < 16)
    {
        p += 2;
        base = 16;
    }
    else if (base == 0)
    {
        base = p[0] == '0' ? 8 : 10;
    }

    const char *digits = p;
    unsigned long long value = 0;
    *overflow = 0;
    while (digit_of(*p) < base)
    {
        // no 64-bit division here: there is no libgcc
        if (__builtin_mul_overflow(value, (unsigned long long)base, &value) ||
            __builtin_add_overflow(value, (unsigned long long)digit_of(*p), &value))
            *overflow = 1;
        p++;
    }
    if (end)
        *end = (char *)(p == digits ? s : p);
    return value;
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    int negative;
    int overflow;
    unsigned long long value = parse_unsigned(s, end, base, &negative, &overflow);
    if (overflow)
    {
        errno = ERANGE;
        return ~0ull;
    }
    return negative ? (unsigned long long)-(long long)value : value;
}

long long strtoll(const char *s, char **end, int base)
{
    int negative;
    int overflow;
    unsigned long long value = parse_unsigned(s, end, base, &negative, &overflow);
    unsigned long long limit = negative ? (1ull << 63) : (1ull << 63) - 1;
    if (overflow || value > limit)
    {
        errno = ERANGE;
        return negative ? (long long)(1ull << 63) : (long long)((1ull << 63) - 1);
    }
    return negative ? -(long long)value : (long long)value;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    int negative;
    int overflow;
    unsigned long long value = parse_unsigned(s, end, base, &negative, &overflow);
    if (overflow || value > 0xFFFFFFFFull)
    {
        errno = ERANGE;
        return 0xFFFFFFFFul;
    }
    return negative ? (unsigned long)-(long)value : (unsigned long)value;
}

long strtol(const char *s, char **end, int base)
{
    long long value = strtoll(s, end, base);
    if (value > 2147483647LL)
    {
        errno = ERANGE;
        return 2147483647L;
    }
    if (value < -2147483647LL - 1)
    {
        errno = ERANGE;
        return -2147483647L - 1;
    }
    return (long)value;
}

long atol(const char *s)
{
    return strtol(s, NULL, 10);
}

long long atoll(const char *s)
{
    return strtoll(s, NULL, 10);
}

static unsigned long long rand_state = 1;

int rand(void)
{
    rand_state = rand_state * 6364136223846793005ull + 1442695040888963407ull;
    return (int)((rand_state >> 33) & RAND_MAX);
}

void srand(unsigned int seed)
{
    rand_state = seed;
}

/**
 * swap two elements of size bytes.
 */
static void swap_bytes(char *a, char *b, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *))
{
    // a heap sort: O(n log n) at worst, no recursion, no extra memory
    char *a = base;
    if (count < 2)
        return;

    for (size_t start = count / 2; start-- > 0;)
    {
        for (size_t root = start; 2 * root + 1 < count;)
        {
            size_t child = 2 * root + 1;
            if (child + 1 < count && compare(a + child * size, a + (child + 1) * size) < 0)
                child++;
            if (compare(a + root * size, a + child * size) >= 0)
                break;
            swap_bytes(a + root * size, a + child * size, size);
            root = child;
        }
    }
    for (size_t end = count - 1; end > 0; end--)
    {
        swap_bytes(a, a + end * size, size);
        for (size_t root = 0; 2 * root + 1 < end;)
        {
            size_t child = 2 * root + 1;
            if (child + 1 < end && compare(a + child * size, a + (child + 1) * size) < 0)
                child++;
            if (compare(a + root * size, a + child * size) >= 0)
                break;
            swap_bytes(a + root * size, a + child * size, size);
            root = child;
        }
    }
}

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *))
{
    const char *a = base;
    size_t low = 0;
    size_t high = count;
    while (low < high)
    {
        size_t middle = low + (high - low) / 2;
        int order = compare(key, a + middle * size);
        if (order == 0)
            return (void *)(a + middle * size);
        if (order < 0)
            high = middle;
        else
            low = middle + 1;
    }
    return NULL;
}

/*
 * the environment: environ starts as the kernel's array; the first change
 * copies it to a heap array the libc owns
 */

static char **owned_environ;
static int environ_count;

static int take_environ(void)
{
    if (owned_environ && environ == owned_environ)
        return 0;
    int count = 0;
    while (environ && environ[count])
        count++;
    char **copy = malloc((size_t)(count + 1) * sizeof(char *));
    if (!copy)
        return -1;
    for (int i = 0; i < count; i++)
        copy[i] = environ[i];
    copy[count] = NULL;
    owned_environ = environ = copy;
    environ_count = count;
    return 0;
}

static int find_variable(const char *name, size_t len)
{
    for (int i = 0; environ && environ[i]; i++)
        if (strncmp(environ[i], name, len) == 0 && environ[i][len] == '=')
            return i;
    return -1;
}

int putenv(char *entry)
{
    char *equals = strchr(entry, '=');
    if (!equals || take_environ() < 0)
    {
        errno = equals ? ENOMEM : EINVAL;
        return -1;
    }
    int index = find_variable(entry, (size_t)(equals - entry));
    if (index >= 0)
    {
        environ[index] = entry;
        return 0;
    }
    char **grown = realloc(owned_environ, (size_t)(environ_count + 2) * sizeof(char *));
    if (!grown)
    {
        errno = ENOMEM;
        return -1;
    }
    owned_environ = environ = grown;
    environ[environ_count++] = entry;
    environ[environ_count] = NULL;
    return 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
    size_t len = strlen(name);
    if (len == 0 || strchr(name, '='))
    {
        errno = EINVAL;
        return -1;
    }
    if (!overwrite && find_variable(name, len) >= 0)
        return 0;

    char *entry = malloc(len + strlen(value) + 2);
    if (!entry)
    {
        errno = ENOMEM;
        return -1;
    }
    strcpy(entry, name);
    entry[len] = '=';
    strcpy(entry + len + 1, value);
    return putenv(entry);
}

int unsetenv(const char *name)
{
    if (take_environ() < 0)
        return -1;
    int index = find_variable(name, strlen(name));
    if (index >= 0)
    {
        environ_count--;
        environ[index] = environ[environ_count];
        environ[environ_count] = NULL;
    }
    return 0;
}

double strtod(const char *str, char **end)
{
    const char *p = str;
    while (isspace((unsigned char)*p))
    {
        p++;
    }

    double sign = 1.0;
    if (*p == '+' || *p == '-')
    {
        sign = *p == '-' ? -1.0 : 1.0;
        p++;
    }

    double value = 0.0;
    int digits = 0;
    while (isdigit((unsigned char)*p))
    {
        value = value * 10.0 + (*p++ - '0');
        digits++;
    }
    if (*p == '.')
    {
        p++;
        double scale = 0.1;
        while (isdigit((unsigned char)*p))
        {
            value += (*p++ - '0') * scale;
            scale *= 0.1;
            digits++;
        }
    }
    if (digits == 0)
    {
        if (end)
        {
            *end = (char *)str;
        }
        return 0.0;
    }

    // an exponent counts only if digits follow it
    if (*p == 'e' || *p == 'E')
    {
        const char *q = p + 1;
        int exp_sign = 1;
        if (*q == '+' || *q == '-')
        {
            exp_sign = *q == '-' ? -1 : 1;
            q++;
        }
        if (isdigit((unsigned char)*q))
        {
            int exponent = 0;
            while (isdigit((unsigned char)*q))
            {
                if (exponent < 10000)
                {
                    exponent = exponent * 10 + (*q - '0');
                }
                q++;
            }
            double factor = exp_sign > 0 ? 10.0 : 0.1;
            while (exponent-- > 0)
            {
                value *= factor;
            }
            p = q;
        }
    }

    if (end)
    {
        *end = (char *)p;
    }
    return sign * value;
}

double atof(const char *str)
{
    return strtod(str, NULL);
}

int system(const char *command)
{
    if (!command)
    {
        return access("/bin/sh", X_OK) == 0;
    }

    pid_t child = fork();
    if (child < 0)
    {
        return -1;
    }
    if (child == 0)
    {
        char *argv[] = {"sh", "-c", (char *)command, NULL};
        execve("/bin/sh", argv, environ);
        _exit(127);
    }

    int status;
    while (waitpid(child, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            return -1;
        }
    }
    return status;
}

int mkstemp(char *template)
{
    static const char letters[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static unsigned long long counter;
    size_t length = strlen(template);
    if (length < 6 || strcmp(template + length - 6, "XXXXXX") != 0)
    {
        errno = EINVAL;
        return -1;
    }
    char *x = template + length - 6;

    for (int tries = 0; tries < 100; tries++)
    {
        // not secret, only different: the time, the pid and a count
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        unsigned long long v = (unsigned long long)now.tv_nsec * 2654435761u ^
                               ((unsigned long long)getpid() << 32) ^ ++counter * 40503u;
        for (int i = 0; i < 6; i++)
        {
            x[i] = letters[v % (sizeof(letters) - 1)];
            v /= sizeof(letters) - 1;
        }
        int fd = open(template, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0 || errno != EEXIST)
            return fd;
    }
    errno = EEXIST;
    return -1;
}

intmax_t strtoimax(const char *s, char **end, int base)
{
    return strtoll(s, end, base);
}

uintmax_t strtoumax(const char *s, char **end, int base)
{
    return strtoull(s, end, base);
}

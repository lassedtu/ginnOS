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
#include "limits.h"
#include "sys/stat.h"

/**
 * @file stdlib.c
 * @brief This file contains the implementations of general utility functions.
 *
 * the memory allocator is in malloc.c.
 */

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

char *realpath(const char *restrict path, char *restrict resolved)
{
    if (!path)
    {
        errno = EINVAL;
        return NULL;
    }
    struct stat st;
    if (stat(path, &st) < 0)
        return NULL;
    char joined[PATH_MAX * 2];
    if (path[0] == '/')
        snprintf(joined, sizeof(joined), "%s", path);
    else
    {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd)))
            return NULL;
        snprintf(joined, sizeof(joined), "%s/%s", cwd, path);
    }
    // the parts one at a time: "." is skipped, ".." takes the last one off
    char out[PATH_MAX];
    size_t length = 0;
    for (char *part = joined; *part;)
    {
        while (*part == '/')
            part++;
        char *end = part;
        while (*end && *end != '/')
            end++;
        size_t n = (size_t)(end - part);
        if (n == 0 || (n == 1 && part[0] == '.'))
        {
        }
        else if (n == 2 && part[0] == '.' && part[1] == '.')
        {
            while (length > 0 && out[length - 1] != '/')
                length--;
            if (length > 0)
                length--; // the slash
        }
        else
        {
            if (length + 1 + n >= sizeof(out))
            {
                errno = ENAMETOOLONG;
                return NULL;
            }
            out[length++] = '/';
            memcpy(out + length, part, n);
            length += n;
        }
        part = end;
    }
    if (length == 0)
        out[length++] = '/';
    out[length] = '\0';
    if (!resolved)
        return strdup(out);
    strcpy(resolved, out);
    return resolved;
}

char *mkdtemp(char *template)
{
    static const char letters[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static unsigned long long counter;
    size_t length = strlen(template);
    if (length < 6 || strcmp(template + length - 6, "XXXXXX") != 0)
    {
        errno = EINVAL;
        return NULL;
    }
    char *x = template + length - 6;
    for (int tries = 0; tries < 100; tries++)
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        unsigned long long v = (unsigned long long)now.tv_nsec * 2654435761u ^
                               ((unsigned long long)getpid() << 32) ^ ++counter * 40503u;
        for (int i = 0; i < 6; i++)
        {
            x[i] = letters[v % (sizeof(letters) - 1)];
            v /= sizeof(letters) - 1;
        }
        if (mkdir(template, 0700) == 0)
            return template;
        if (errno != EEXIST)
            return NULL;
    }
    errno = EEXIST;
    return NULL;
}

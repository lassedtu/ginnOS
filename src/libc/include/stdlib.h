#pragma once

#include <sys/types.h>

/**
 * @file stdlib.h
 * @brief This file contains the declarations of general utility functions.
 */

#ifndef NULL
#define NULL ((void *)0)
#endif

/**
 * allocate memory.
 * @param size number of bytes to allocate.
 * @return pointer to allocated memory, or NULL on failure.
 */
void *malloc(size_t size);

/**
 * allocate zero-initialized memory.
 * @param nmemb number of elements.
 * @param size size of each element in bytes.
 * @return pointer to allocated memory, or NULL on failure.
 */
void *calloc(size_t nmemb, size_t size);

/**
 * resize a previously allocated block.
 * @param ptr pointer returned by malloc/calloc/realloc, or NULL.
 * @param size new size in bytes. if 0, the block is freed and NULL is returned.
 * @return pointer to the resized block, or NULL on failure (original block unchanged).
 */
void *realloc(void *ptr, size_t size);

/**
 * free a previously allocated block.
 * @param ptr pointer returned by malloc/calloc/realloc, or NULL (no-op).
 */
void free(void *ptr);

/**
 * convert a string to an integer.
 * @param s null-terminated string.
 * @return the integer value.
 */
int atoi(const char *s);

/**
 * the environment: a NULL-terminated array of "KEY=VALUE" strings.
 */
extern char **environ;

/**
 * look up an environment variable.
 * @param name the variable name (without '=').
 * @return its value, or NULL if it is not set.
 */
char *getenv(const char *name);

/**
 * register a function for exit() to call. handlers run in the reverse
 * order of registration; _exit() skips them.
 * @param function the handler.
 * @return 0, or -1 if 32 handlers are registered already.
 */
int atexit(void (*function)(void));

typedef struct
{
    int quot;
    int rem;
} div_t;

typedef struct
{
    long quot;
    long rem;
} ldiv_t;

#define RAND_MAX 0x7fffffff
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

long atol(const char *s);
long long atoll(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);

int abs(int value);
long labs(long value);
long long llabs(long long value);
div_t div(int numerator, int denominator);
ldiv_t ldiv(long numerator, long denominator);

int rand(void);
void srand(unsigned int seed);

/**
 * sort count elements of size bytes (heap sort: not stable).
 */
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));

/**
 * find key in a sorted array.
 * @return the element, or NULL.
 */
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));

int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *entry);

/**
 * end the process with SIGABRT.
 */
void abort(void) __attribute__((noreturn));

/**
 * run the atexit handlers, write out buffered stdio output, then end the
 * process with an exit code.
 * @param status exit code.
 */
void exit(int status) __attribute__((noreturn));

/**
 * @file malloc_test.h
 * @brief what malloc.c needs on the host (with -DMALLOC_TEST).
 *
 * the functions get other names, so that they do not take the place of
 * the host's malloc; sbrk and mmap come from the test.
 */

#ifndef MALLOC_TEST_H
#define MALLOC_TEST_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define malloc  test_malloc
#define free    test_free
#define calloc  test_calloc
#define realloc test_realloc
#define sbrk    test_sbrk
#define mmap    test_mmap
#define munmap  test_munmap

// the host's <sys/mman.h> can have these already (the test includes it)
#ifndef PROT_READ
#define PROT_READ     1
#define PROT_WRITE    2
#define MAP_PRIVATE   2
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED    ((void *)-1)
#endif

void *test_malloc(size_t bytes);
void test_free(void *p);
void *test_calloc(size_t count, size_t size);
void *test_realloc(void *p, size_t bytes);
int heap_check(void);

void *test_sbrk(int increment);
void *test_mmap(void *address, size_t length, int prot, int flags, int fd, long offset);
int test_munmap(void *address, size_t length);

#endif

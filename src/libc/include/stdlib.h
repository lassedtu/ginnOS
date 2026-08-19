#pragma once

/**
 * @file stdlib.h
 * @brief This file contains the declarations of general utility functions.
 */

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef unsigned int size_t;

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
 * terminate the process with an exit code.
 * @param status exit code.
 */
void exit(int status) __attribute__((noreturn));

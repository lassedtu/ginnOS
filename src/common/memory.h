#pragma once

#include "stdint.h"

/**
 * copy memory from source to destination.
 * @param dest destination buffer.
 * @param src source buffer.
 * @param count number of bytes to copy.
 * @return pointer to the destination buffer.
 */
void *memcpy(void *dest, const void *src, uint32_t count);

/**
 * move memory from source to destination, allowing for overlapping regions.
 * @param dest destination buffer.
 * @param src source buffer.
 * @param count number of bytes to move.
 * @return pointer to the destination buffer.
 */
void *memmove(void *dest, const void *src, uint32_t count);

/**
 * set a block of memory to a specific value.
 * @param dest destination buffer.
 * @param value value to set each byte to.
 * @param count number of bytes to set.
 * @return pointer to the destination buffer.
 */
void *memset(void *dest, int value, uint32_t count);

/**
 * compare two blocks of memory.
 * @param lhs left hand side buffer.
 * @param rhs right hand side buffer.
 * @param count number of bytes to compare.
 * @return negative if lhs < rhs, 0 if lhs == rhs, positive if lhs > rhs.
 */
int memcmp(const void *lhs, const void *rhs, uint32_t count);

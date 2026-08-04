#pragma once

#include "stdint.h"

/**
 * compute the length of a null terminated string.
 * @param str null terminated string.
 * @return length of the string excluding the null terminator.
 */
uint32_t strlen(const char *str);

/**
 * compare two null terminated strings.
 * @param lhs left hand side string.
 * @param rhs right hand side string.
 * @return negative if lhs < rhs, 0 if lhs == rhs, positive if lhs > rhs.
 */
int strcmp(const char *lhs, const char *rhs);

/**
 * compare two null terminated strings up to a maximum number of characters.
 * @param lhs left hand side string.
 * @param rhs right hand side string.
 * @param count maximum number of characters to compare.
 * @return negative if lhs < rhs, 0 if lhs == rhs, positive if lhs > rhs.
 */
int strncmp(const char *lhs, const char *rhs, uint32_t count);

/**
 * copy a null terminated string to a destination buffer.
 * @param dest destination buffer.
 * @param src source string.
 * @return pointer to the destination buffer.
 */
char *strcpy(char *dest, const char *src);

/**
 * copy a null terminated string to a destination buffer up to a maximum number of characters.
 * @param dest destination buffer.
 * @param src source string.
 * @param count maximum number of characters to copy.
 * @return pointer to the destination buffer.
 */
char *strncpy(char *dest, const char *src, uint32_t count);

#pragma once

/**
 * @file string.h
 * @brief This file contains the declarations of string manipulation functions.
 */

typedef unsigned int size_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/**
 * @brief computes the length of the string s.
 * @param s the string to compute the length of.
 * @returns the length of the string s, excluding the terminating null byte ('\0').
 */
size_t strlen(const char *s);

/**
 * @brief compares the string s1 to the string s2.
 * @param s1 the first string to compare.
 * @param s2 the second string to compare.
 * @returns an integer less than, equal to, or greater than zero if s1 is
 */
int strcmp(const char *s1, const char *s2);

/**
 * @brief compares the first n bytes of the string s1 to the first n bytes of the string s2.
 * @param s1 the first string to compare.
 * @param s2 the second string to compare.
 * @param n the number of bytes to compare.
 * @returns an integer less than, equal to, or greater than zero if the first n
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * @brief copies the string pointed to by src, including the terminating null byte ('\0'), to the buffer pointed to by dst.
 * @param dst the destination buffer.
 * @param src the source string to copy.
 * @returns a pointer to the destination string dst.
 */
char *strcpy(char *dst, const char *src);

/**
 * @brief copies up to n bytes from the string pointed to by src to the buffer pointed to by dst.
 * @param dst the destination buffer.
 * @param src the source string to copy.
 * @param n the maximum number of bytes to copy.
 * @returns a pointer to the destination string dst.
 */
char *strncpy(char *dst, const char *src, size_t n);

/**
 * @brief appends the string pointed to by src to the end of the string pointed to by dst.
 * @param dst the destination string to which src will be appended.
 * @param src the source string to append.
 * @returns a pointer to the destination string dst.
 */
char *strcat(char *dst, const char *src);

/**
 * @brief appends up to n bytes from the string pointed to by src to the end of the string pointed to by dst.
 * @param dst the destination string to which src will be appended.
 * @param src the source string to append.
 * @param n the maximum number of bytes to append.
 * @returns a pointer to the destination string dst.
 */
char *strchr(const char *s, int c);

/**
 * @brief locates the last occurrence of c (converted to a char) in the string pointed to by s.
 * @param s the string to search.
 * @param c the character to locate.
 * @returns a pointer to the last occurrence of c in s, or NULL if c is not found.
 */
char *strrchr(const char *s, int c);

/**
 * @brief tokenizes the string str using the delimiters specified in delim.
 * @param str the string to tokenize. On the first call, str should point to the string to tokenize. On subsequent calls, str should be NULL.
 * @param delim a string containing the delimiters to use for tokenization.
 * @returns a pointer to the next token found in str, or NULL if no more tokens are found.
 */
char *strtok(char *str, const char *delim);

/**
 * @brief copies n bytes from memory area src to memory area dst. The memory areas must not overlap.
 * @param dst the destination memory area.
 * @param src the source memory area.
 * @param n the number of bytes to copy.
 * @returns a pointer to the destination memory area dst.
 */
void *memcpy(void *dst, const void *src, size_t n);

/**
 * @brief fills the first n bytes of the memory area pointed to by dst with the constant byte c.
 * @param dst the memory area to fill.
 * @param c the byte to fill the memory area with.
 * @param n the number of bytes to fill.
 * @returns a pointer to the memory area dst.
 */
void *memset(void *dst, int c, size_t n);

/**
 * @brief compares the first n bytes of the memory areas s1 and s2.
 * @param s1 the first memory area to compare.
 * @param s2 the second memory area to compare.
 * @param n the number of bytes to compare.
 * @returns an integer less than, equal to, or greater than zero if the first n bytes of s1 are found, respectively, to be less than, to match, or be greater than the first n bytes of s2.
 */
int memcmp(const void *s1, const void *s2, size_t n);

/**
 * @brief copies n bytes from memory area src to memory area dst. The memory areas may overlap.
 * @param dst the destination memory area.
 * @param src the source memory area.
 * @param n the number of bytes to copy.
 * @returns a pointer to the destination memory area dst.
 */
void *memmove(void *dst, const void *src, size_t n);

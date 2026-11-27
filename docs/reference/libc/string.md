<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;string.h&gt;

Source: [`src/libc/include/string.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/string.h)

This file contains the declarations of string manipulation functions.

## strlen

```c
size_t strlen(const char *s);
```

computes the length of the string s.

- `s`: the string to compute the length of.

Returns: the length of the string s, excluding the terminating null byte ('\0').

## strcmp

```c
int strcmp(const char *s1, const char *s2);
```

compares the string s1 to the string s2.

- `s1`: the first string to compare.
- `s2`: the second string to compare.

Returns: an integer less than, equal to, or greater than zero if s1 is

## strncmp

```c
int strncmp(const char *s1, const char *s2, size_t n);
```

compares the first n bytes of the string s1 to the first n bytes of the string s2.

- `s1`: the first string to compare.
- `s2`: the second string to compare.
- `n`: the number of bytes to compare.

Returns: an integer less than, equal to, or greater than zero if the first n

## strcpy

```c
char *strcpy(char *dst, const char *src);
```

copies the string pointed to by src, including the terminating null byte ('\0'), to the buffer pointed to by dst.

- `dst`: the destination buffer.
- `src`: the source string to copy.

Returns: a pointer to the destination string dst.

## strncpy

```c
char *strncpy(char *dst, const char *src, size_t n);
```

copies up to n bytes from the string pointed to by src to the buffer pointed to by dst.

- `dst`: the destination buffer.
- `src`: the source string to copy.
- `n`: the maximum number of bytes to copy.

Returns: a pointer to the destination string dst.

## strcat

```c
char *strcat(char *dst, const char *src);
```

appends the string pointed to by src to the end of the string pointed to by dst.

- `dst`: the destination string to which src will be appended.
- `src`: the source string to append.

Returns: a pointer to the destination string dst.

## strchr

```c
char *strchr(const char *s, int c);
```

appends up to n bytes from the string pointed to by src to the end of the string pointed to by dst.

- `dst`: the destination string to which src will be appended.
- `src`: the source string to append.
- `n`: the maximum number of bytes to append.

Returns: a pointer to the destination string dst.

## strrchr

```c
char *strrchr(const char *s, int c);
```

locates the last occurrence of c (converted to a char) in the string pointed to by s.

- `s`: the string to search.
- `c`: the character to locate.

Returns: a pointer to the last occurrence of c in s, or NULL if c is not found.

## strtok

```c
char *strtok(char *str, const char *delim);
```

tokenizes the string str using the delimiters specified in delim.

- `str`: the string to tokenize. On the first call, str should point to the string to tokenize. On subsequent calls, str should be NULL.
- `delim`: a string containing the delimiters to use for tokenization.

Returns: a pointer to the next token found in str, or NULL if no more tokens are found.

## memcpy

```c
void *memcpy(void *dst, const void *src, size_t n);
```

copies n bytes from memory area src to memory area dst. The memory areas must not overlap.

- `dst`: the destination memory area.
- `src`: the source memory area.
- `n`: the number of bytes to copy.

Returns: a pointer to the destination memory area dst.

## memset

```c
void *memset(void *dst, int c, size_t n);
```

fills the first n bytes of the memory area pointed to by dst with the constant byte c.

- `dst`: the memory area to fill.
- `c`: the byte to fill the memory area with.
- `n`: the number of bytes to fill.

Returns: a pointer to the memory area dst.

## memcmp

```c
int memcmp(const void *s1, const void *s2, size_t n);
```

compares the first n bytes of the memory areas s1 and s2.

- `s1`: the first memory area to compare.
- `s2`: the second memory area to compare.
- `n`: the number of bytes to compare.

Returns: an integer less than, equal to, or greater than zero if the first n bytes of s1 are found, respectively, to be less than, to match, or be greater than the first n bytes of s2.

## memmove

```c
void *memmove(void *dst, const void *src, size_t n);
```

copies n bytes from memory area src to memory area dst. The memory areas may overlap.

- `dst`: the destination memory area.
- `src`: the source memory area.
- `n`: the number of bytes to copy.

Returns: a pointer to the destination memory area dst.

## strsignal

```c
char *strsignal(int sig);
```

a description of a signal ("Terminated"), or "Unknown signal N"

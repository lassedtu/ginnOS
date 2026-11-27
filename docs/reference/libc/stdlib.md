<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;stdlib.h&gt;

Source: [`src/libc/include/stdlib.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/stdlib.h)

This file contains the declarations of general utility functions.

## malloc

```c
void *malloc(size_t size);
```

allocate memory.

- `size`: number of bytes to allocate.

Returns: pointer to allocated memory, or NULL on failure.

## calloc

```c
void *calloc(size_t nmemb, size_t size);
```

allocate zero-initialized memory.

- `nmemb`: number of elements.
- `size`: size of each element in bytes.

Returns: pointer to allocated memory, or NULL on failure.

## realloc

```c
void *realloc(void *ptr, size_t size);
```

resize a previously allocated block.

- `ptr`: pointer returned by malloc/calloc/realloc, or NULL.
- `size`: new size in bytes. if 0, the block is freed and NULL is returned.

Returns: pointer to the resized block, or NULL on failure (original block unchanged).

## free

```c
void free(void *ptr);
```

free a previously allocated block.

- `ptr`: pointer returned by malloc/calloc/realloc, or NULL (no-op).

## atoi

```c
int atoi(const char *s);
```

convert a string to an integer.

- `s`: null-terminated string.

Returns: the integer value.

## strtod, strtof, strtold

```c
double strtod(const char *str, char **end);
float strtof(const char *str, char **end);
long double strtold(const char *str, char **end);
```

a floating-point number at the start of str, after spaces: an optional
sign, then decimal digits with an optional point and exponent (1.5e3),
hexadecimal digits with "0x" and an optional "p" exponent (0x1.8p1),
"inf", "infinity" or "nan". the result is rounded correctly.

- `end`: receives the first character after the number (may be NULL).

Returns: the number, or 0 if there is none (then \*end is str). a number past the largest gives HUGE\_VAL, below the smallest 0; both set errno to ERANGE.

## atof

```c
double atof(const char *str);
```

strtod(str, NULL).

## system

```c
int system(const char *command);
```

run a command with "/bin/sh -c" and wait for it.

- `command`: the command line, or NULL to ask whether there is a shell.

Returns: the wait status of the shell, or -1 if it could not run (for NULL: non-zero if /bin/sh exists).

## mkstemp

```c
int mkstemp(char *template);
```

make and open a new file with a name no other file has. the last six
characters of template must be "XXXXXX"; they are changed to the name
that was used.

Returns: an fd open for reading and writing (mode 0600), or -1 (errno set).

## environ

```c
extern char **environ;
```

the environment: a NULL-terminated array of "KEY=VALUE" strings.

## getenv

```c
char *getenv(const char *name);
```

look up an environment variable.

- `name`: the variable name (without '=').

Returns: its value, or NULL if it is not set.

## function

```c
int atexit(void (*function)(void));
```

register a function for exit() to call. handlers run in the reverse
order of registration; \_exit() skips them.

- `function`: the handler.

Returns: 0, or -1 if 32 handlers are registered already.

## arc4random, arc4random\_buf

```c
uint32_t arc4random(void);
void arc4random_buf(void *buf, size_t length);
```

random numbers good for keys, from the kernel's generator
(sys/random.h). they cannot fail.

## arc4random\_uniform

```c
uint32_t arc4random_uniform(uint32_t bound);
```

a random number below bound, each one equally likely

## compare

```c
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
```

sort count elements of size bytes (heap sort: not stable).

## compare (2)

```c
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));
```

find key in a sorted array.

Returns: the element, or NULL.

## posix\_openpt, grantpt, unlockpt, ptsname, ptsname\_r

```c
int posix_openpt(int flags);
int grantpt(int fd);
int unlockpt(int fd);
char *ptsname(int fd);
int ptsname_r(int fd, char *buf, size_t size);
```

pseudo-terminals (the master side, /dev/ptmx). posix\_openpt opens a new
master; unlockpt lets the slave be opened; ptsname gives the slave's
path (in a static buffer). grantpt does nothing: the slaves are open to
all. see also &lt;pty.h&gt;.

## abort

```c
void abort(void) __attribute__((noreturn));
```

end the process with SIGABRT.

## exit

```c
void exit(int status) __attribute__((noreturn));
```

run the atexit handlers, write out buffered stdio output, then end the
process with an exit code.

- `status`: exit code.

## \_Exit

```c
void _Exit(int status) __attribute__((noreturn));
```

end the process at once: no atexit functions, no stream flush (C99)

## mblen

```c
int mblen(const char *s, size_t n);
```

the bytes of the character at s (at most n), 0 for "\0", or -1

## mbtowc

```c
int mbtowc(wchar_t *wc, const char *s, size_t n);
```

the character at s (at most n bytes) into \*wc: its bytes, 0 or -1

## wctomb

```c
int wctomb(char *s, wchar_t wc);
```

wc as UTF-8 at s: the bytes, or -1

## realpath

```c
char *realpath(const char *restrict path, char *restrict resolved);
```

the absolute path of path, with no "." or ".." and no double slashes,
into resolved (PATH\_MAX bytes), or into new memory when resolved is
NULL. the file must exist. (ginnOS has no symbolic links yet.)

Returns: resolved, or NULL with errno.

## mkdtemp

```c
char *mkdtemp(char *template);
```

make a new folder (0700) named template with its six X's replaced

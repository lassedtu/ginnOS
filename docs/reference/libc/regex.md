<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;regex.h&gt;

Source: [`src/libc/include/regex.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/regex.h)

POSIX regular expressions, basic (BRE) and extended (ERE). The
       matcher is musl's (from TRE): see src/libc/src/regex/README.md.

## regex\_t

```c
typedef struct re_pattern_buffer
{
    size_t re_nsub; // the parenthesised parts
    void *__opaque;
    void *__padding[4];
    size_t __nsub2;
    char __padding2;
} regex_t;
```

a compiled expression (the layout is musl's)

## regmatch\_t

```c
typedef struct
{
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;
```

where a match (or a part of it) starts and ends; -1 when it did not match

## regcomp

```c
int regcomp(regex_t *restrict preg, const char *restrict pattern, int cflags);
```

compile pattern into preg: REG\_OK, or an error for regerror

## regexec, regfree

```c
int regexec(const regex_t *restrict preg, const char *restrict text, size_t nmatch,
             regmatch_t *restrict pmatch, int eflags);
void regfree(regex_t *preg);
```

match text against preg: REG\_OK or REG\_NOMATCH. pmatch gets where the
match and the first nmatch - 1 parts are.

## regerror

```c
size_t regerror(int error, const regex_t *restrict preg, char *restrict buffer, size_t size);
```

the message for an error into buffer (size bytes, cut if it is longer).

Returns: the size the whole message needs, with its null byte.

<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;fnmatch.h&gt;

Source: [`src/libc/include/fnmatch.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/fnmatch.h)

shell patterns ("\*.c", "\[a-z]?"): fnmatch is musl's (see
       src/libc/src/regex/README.md).

## fnmatch

```c
int fnmatch(const char *pattern, const char *name, int flags);
```

0 when name matches pattern, FNM\_NOMATCH when it does not

<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;utime.h&gt;

Source: [`src/libc/include/utime.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/utime.h)

utime: set a file's times in whole seconds (the older call; see
       utimensat in &lt;sys/stat.h&gt;).

## utime

```c
int utime(const char *path, const struct utimbuf *times);
```

set a file's access and modification times; with times NULL, both to
the time now.

Returns: 0, or -1 (errno set).

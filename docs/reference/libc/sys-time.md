<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/time.h&gt;

Source: [`src/libc/include/sys/time.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/time.h)

struct timeval and gettimeofday. it also gives select() and
       fd\_set (sys/select.h), as on other Unix systems.

## gettimeofday

```c
int gettimeofday(struct timeval *tv, void *tz);
```

the wall-clock time in seconds and microseconds since 1970.

- `tv`: receives it.
- `tz`: must be NULL (there are no time zones).

Returns: 0, or -1 (errno set).

## utimes

```c
int utimes(const char *path, const struct timeval times[2]);
```

set a file's access and modification times (times\[0], times\[1]) in
microseconds; with times NULL, both to the time now.

Returns: 0, or -1 (errno set).

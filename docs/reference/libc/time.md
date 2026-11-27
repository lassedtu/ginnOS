<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;time.h&gt;

Source: [`src/libc/include/time.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/time.h)

clocks, sleeping and calendar time (UTC only: there are no time
zones, so local time is UTC).

struct timespec and the clock ids are ABI (src/kernel/time/clock.h).

## timespec\_get

```c
int timespec_get(struct timespec *out, int base);
```

the time now (C11).

Returns: base (TIME\_UTC), or 0 for another base.

## clock\_settime, nanosleep, clock, difftime

```c
int clock_settime(clockid_t id, const struct timespec *when);
int nanosleep(const struct timespec *span, struct timespec *left);
clock_t clock(void);
double difftime(time_t end, time_t start);
```

set the realtime clock (root only; whole seconds)

## strftime

```c
size_t strftime(char *buf, size_t size, const char *format, const struct tm *tm);
```

format a time. supports %a %A %b %B %c %d %e %H %I %j %m %M %n %p %S %t
%u %w %y %Y %Z %z %F %T %D %R and %%.

Returns: the length written, or 0 if it does not fit.

## strptime

```c
char *strptime(const char *restrict text, const char *restrict format, struct tm *restrict tm);
```

read a time from text as format says (the strftime conversions, and
spaces in the format match any spaces) into the fields of tm it names.

Returns: the first character not read, or NULL when the text does not fit.

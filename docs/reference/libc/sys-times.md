<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/times.h&gt;

Source: [`src/libc/include/sys/times.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/times.h)

process CPU times.

the four times are in clock ticks: sysconf-free ginnOS has 100 per
second (CLK\_TCK). struct tms is ABI (kernel/time/clock.c).

## times

```c
clock_t times(struct tms *buf);
```

the CPU times of the process and its reaped children.

Returns: the ticks since boot.

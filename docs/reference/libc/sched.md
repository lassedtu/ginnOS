<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sched.h&gt;

Source: [`src/libc/include/sched.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sched.h)

the scheduler: give up the CPU. ginnOS has one policy for all
       processes (the nice value changes the share of each).

## sched\_yield

```c
int sched_yield(void);
```

give the CPU to another thread that is ready to run.

Returns: 0.

## sched\_get\_priority\_min, sched\_get\_priority\_max

```c
int sched_get_priority_min(int policy);
int sched_get_priority_max(int policy);
```

the range of priorities of a policy: 0 to 0 for SCHED\_OTHER, 1 to 99
for the real-time policies (as on Linux).

Returns: the priority, or -1 with errno EINVAL for another policy.

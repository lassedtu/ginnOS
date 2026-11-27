<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/resource.h&gt;

Source: [`src/libc/include/sys/resource.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/resource.h)

process priorities: the nice value, -20 (the most CPU) to 19
       (the least). ginnOS gives a process with a lower nice value a
       longer time slice.

## getpriority

```c
int getpriority(int which, id_t who);
```

the lowest nice value of the processes which and who name.

Returns: the value, or -1 with errno set (-1 is also a nice value: set errno to 0 first to tell them apart).

## setpriority

```c
int setpriority(int which, id_t who, int value);
```

set the nice value of processes. only root may lower it.

Returns: 0, or -1 (errno: ESRCH, EPERM, EACCES).

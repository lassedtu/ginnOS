<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;syslog.h&gt;

Source: [`src/libc/include/syslog.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/syslog.h)

the system log: ginnOS writes each message to the kernel log
       (/dev/kmsg), with the name, the pid and the level before it.

## setlogmask

```c
int setlogmask(int mask);
```

the levels to write (LOG\_MASK bits); returns the old mask (0: no change)

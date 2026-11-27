<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;termios.h&gt;

Source: [`src/libc/include/termios.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/termios.h)

terminal settings.

the layout, flags and values are ABI and must match the kernel side in
src/kernel/console/termios.h exactly (they are the Linux i386 ones).

## cfmakeraw

```c
void cfmakeraw(struct termios *t);
```

settings for byte-by-byte input with no echo, no signals and no output
processing.

## tcgetpgrp, tcsetpgrp

```c
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgrp);
```

the foreground process group of the terminal on fd, and setting it.

<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;pty.h&gt;

Source: [`src/libc/include/pty.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/pty.h)

open a pseudo-terminal pair, and make one a process's terminal.

## openpty

```c
int openpty(int *master, int *slave, char *name, const struct termios *settings,
            const struct winsize *size);
```

open a new pseudo-terminal pair.

- `master`: receives the master fd.
- `slave`: receives the slave fd.
- `name`: receives the slave's path, if not NULL (at least 16 bytes).
- `settings`: the slave's termios, if not NULL.
- `size`: the window size, if not NULL.

Returns: 0, or -1 with errno set.

## login\_tty

```c
int login_tty(int fd);
```

make fd the terminal of the calling process: start a new session, make
fd its controlling terminal, put fd on stdin, stdout and stderr, and
close fd. for the child of a terminal app, before execve.

Returns: 0, or -1 with errno set.

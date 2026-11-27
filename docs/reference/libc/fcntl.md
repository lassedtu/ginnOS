<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;fcntl.h&gt;

Source: [`src/libc/include/fcntl.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/fcntl.h)

file open flags and open().

the O\_ values are ABI and must match the kernel side in
src/kernel/vfs/file.h exactly.

## fcntl

```c
int fcntl(int fd, int cmd, ...);
```

control an open file descriptor.

- `fd`: the descriptor.
- `cmd`: an F\_ command.
- `...`: the command's int argument, if it takes one.

Returns: the command's result, or -1 (errno set).

## open

```c
int open(const char *path, int flags, ...);
```

open a file.

- `path`: the file's path.
- `flags`: one access mode, plus creation and status flags.
- `...`: with O\_CREAT, the mode\_t for a new file.

Returns: the lowest free file descriptor, or -1 (errno set).

## creat

```c
int creat(const char *path, mode_t mode);
```

create a file, or truncate it if it exists, and open it for writing:
open(path, O\_WRONLY | O\_CREAT | O\_TRUNC, mode).

Returns: a file descriptor, or -1 (errno set).

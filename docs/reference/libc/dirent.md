<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;dirent.h&gt;

Source: [`src/libc/include/dirent.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/dirent.h)

reading directories: opendir, readdir, closedir.

built on the getdents syscall, whose record layout and DT\_ values are ABI
(src/kernel/vfs/dirent.h).

## opendir

```c
DIR *opendir(const char *path);
```

open a directory for reading.

Returns: the stream, or NULL (errno set).

## fdopendir

```c
DIR *fdopendir(int fd);
```

a directory stream on an open directory (closedir closes fd)

## readdir

```c
struct dirent *readdir(DIR *dir);
```

read the next entry. the returned struct is overwritten by the next call.

Returns: the entry, or NULL at the end of the directory or on error.

## closedir

```c
int closedir(DIR *dir);
```

close a directory stream and its file descriptor.

Returns: 0, or -1 (errno set).

## dirfd

```c
int dirfd(DIR *dir);
```

the file descriptor under a directory stream.

## getdents

```c
int getdents(int fd, void *buf, unsigned int size);
```

read raw getdents records (the syscall under readdir).

Returns: bytes read, 0 at the end, or -1 (errno set).

<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/stat.h&gt;

Source: [`src/libc/include/sys/stat.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/stat.h)

file metadata: stat() and fstat().

struct stat and the S\_ bits are ABI and must match the kernel side in
src/kernel/vfs/stat.h exactly.

## stat

```c
int stat(const char *path, struct stat *st);
```

get a file's metadata by path.

Returns: 0, or -1 (errno set).

## lstat

```c
int lstat(const char *path, struct stat *st);
```

the metadata of a symbolic link itself (stat gives what it points at).

## fstat

```c
int fstat(int fd, struct stat *st);
```

get an open file's metadata (pipes and the console too).

Returns: 0, or -1 (errno set).

## mkdir, mkdirat, fstatat, fchmodat

```c
int mkdir(const char *path, mode_t mode);
int mkdirat(int dirfd, const char *path, mode_t mode);
int fstatat(int dirfd, const char *path, struct stat *st, int flags);
int fchmodat(int dirfd, const char *path, mode_t mode, int flags);
```

create a directory with a mode (taken through the umask).

## chmod, fchmod

```c
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
```

change the permission bits of a file (owner or root only).

## umask

```c
mode_t umask(mode_t mask);
```

set the mask taken off the mode of new files.

Returns: the old mask.

## utimensat, futimens

```c
int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags);
int futimens(int fd, const struct timespec times[2]);
```

set a file's access and modification times (times\[0], times\[1]); with
times NULL, both to the time now.

- `flags`: AT\_SYMLINK\_NOFOLLOW: the link itself.

Returns: 0, or -1 (errno set).

## mkfifo, mkfifoat

```c
int mkfifo(const char *path, mode_t mode);
int mkfifoat(int dirfd, const char *path, mode_t mode);
```

make a named pipe (a FIFO): processes that open it share one pipe.

Returns: 0, or -1 (errno set).

## mknod, mknodat

```c
int mknod(const char *path, mode_t mode, dev_t dev);
int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev);
```

make a named pipe (S\_IFIFO), an empty file (S\_IFREG) or, as root, a
device node (S\_IFCHR, S\_IFBLK with makedev's number). a device node
outside /dev opens with ENXIO: ginnOS's devices are in /dev.

Returns: 0, or -1 (errno set).

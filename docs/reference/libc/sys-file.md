<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/file.h&gt;

Source: [`src/libc/include/sys/file.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/file.h)

flock: advisory locks on whole files.

a lock belongs to the open file (dup and fork share it) and goes when
the file is unlocked or its last descriptor closes. the locks only stop
other flock calls, not reads or writes.

## flock

```c
int flock(int fd, int operation);
```

take, change or release the lock of the file open as fd.

Returns: 0, or -1 (errno: EWOULDBLOCK, EINTR, EBADF, EINVAL, ENOLCK).

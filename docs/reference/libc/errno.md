<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;errno.h&gt;

Source: [`src/libc/include/errno.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/errno.h)

## ENOSYS, EBADF, EINVAL, ENOMEM, ENOENT, EACCES, EPIPE, ESPIPE, EEXIST, EISDIR, ENOTDIR, EMFILE, ENFILE, EIO, ENOSPC, EBUSY, ERANGE, EFAULT, ESRCH, ECHILD, ENOBUFS, ENOTTY, ENODEV, EINTR, EAGAIN, E2BIG, ENOEXEC, ENOTEMPTY, EXDEV, ENAMETOOLONG, EPERM, ELOOP, EROFS, ETIMEDOUT, ENOTSOCK, EADDRINUSE, ECONNREFUSED, EISCONN, ENOTCONN, EAFNOSUPPORT, EPROTONOSUPPORT, EOPNOTSUPP, ECANCELED, EILSEQ, EMLINK, EFBIG, ENOLCK, ENXIO, EDEADLK, EOVERFLOW, EADDRNOTAVAIL, EDESTADDRREQ, EMSGSIZE, ENETUNREACH, EHOSTUNREACH, ENOPROTOOPT, EPROTOTYPE, ECONNRESET, ECONNABORTED, EINPROGRESS, EALREADY, ENETDOWN, EPROTO

```c
#define ENOSYS  1  /* function not implemented */
#define EBADF   2  /* bad file descriptor */
#define EINVAL  3  /* invalid argument */
#define ENOMEM  4  /* not enough memory */
#define ENOENT  5  /* no such file or directory */
#define EACCES  6  /* permission denied */
#define EPIPE   7  /* broken pipe */
#define ESPIPE  8  /* invalid seek */
#define EEXIST  9  /* file exists */
#define EISDIR  10 /* is a directory */
#define ENOTDIR 11 /* not a directory */
#define EMFILE  12 /* too many open files */
#define ENFILE  13 /* too many open files in system */
#define EIO     14 /* i/o error */
#define ENOSPC  15 /* no space left on device */
#define EBUSY   16 /* resource busy */
#define ERANGE  17 /* result too large */
#define EFAULT  18 /* bad address */
#define ESRCH   19 /* no such process */
#define ECHILD  20 /* no child processes */
#define ENOBUFS 21 /* no buffer space available */
#define ENOTTY  22 /* inappropriate ioctl for device */
#define ENODEV  23 /* no such device */
#define EINTR   24 /* interrupted system call */
#define EAGAIN  25 /* resource temporarily unavailable */
#define E2BIG   26 /* argument list too long */
#define ENOEXEC 27 /* exec format error */
#define ENOTEMPTY 28 /* directory not empty */
#define EXDEV   29 /* cross-device link */
#define ENAMETOOLONG 30 /* file name too long */
#define EPERM   31 /* operation not permitted */
#define ELOOP   32 /* too many levels of symbolic links */
#define EROFS   33 /* read-only file system */
#define ETIMEDOUT 34 /* timed out */
#define ENOTSOCK 35 /* not a socket */
#define EADDRINUSE 36 /* address in use */
#define ECONNREFUSED 37 /* connection refused */
#define EISCONN 38 /* already connected */
#define ENOTCONN 39 /* not connected */
#define EAFNOSUPPORT 40 /* address family not supported */
#define EPROTONOSUPPORT 41 /* protocol not supported */
#define EOPNOTSUPP 42 /* operation not supported */
#define ECANCELED 43 /* operation cancelled (by the program, not the kernel) */
#define EILSEQ 44 /* bytes that are not a character (UTF-8), from the libc */
#define EMLINK 45 /* too many links to a file */
#define EFBIG  46 /* a file would be too large */
#define ENOLCK 47 /* no locks left */
#define ENXIO  48 /* no such device or address */
#define EDEADLK 49 /* the lock would never be free (pthreads, from the libc) */
#define EOVERFLOW 50 /* a value would be too large for its type */
#define EADDRNOTAVAIL 51 /* address not available */
#define EDESTADDRREQ 52 /* destination address required */
#define EMSGSIZE 53 /* message too long */
#define ENETUNREACH 54 /* network unreachable */
#define EHOSTUNREACH 55 /* host unreachable */
#define ENOPROTOOPT 56 /* protocol not available */
#define EPROTOTYPE 57 /* wrong protocol type for socket */
#define ECONNRESET 58 /* connection reset by peer */
#define ECONNABORTED 59 /* connection aborted */
#define EINPROGRESS 60 /* operation in progress */
#define EALREADY 61 /* operation already in progress */
#define ENETDOWN 62 /* network is down */
#define EPROTO 63 /* protocol error */
```

posix-compatible errno constants for ginnOS userspace. the numbers are
ABI: src/kernel/errno.h has the same ones.

syscalls return negative errno values on failure. the libc wrappers
extract the error, store it in errno (one for each thread), and return -1
(or NULL, etc.) to the caller.

## \_\_errno\_location

```c
int *__errno_location(void);
```

the calling thread's errno. each thread has its own (thread-local
storage), as the functions of one thread must not see the errors of
another.

Returns: the address of the errno of the calling thread.

## perror

```c
void perror(const char *s);
```

print a message followed by the string for the current errno.

- `s`: prefix string (may be NULL).

## strerror

```c
const char *strerror(int errnum);
```

return a string describing the given error number.

- `errnum`: error number (one of the E\* constants above).

Returns: pointer to a static string.

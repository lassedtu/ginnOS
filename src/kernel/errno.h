#pragma once

#include "common/error.h"

/**
 * errno values the kernel returns to user space, negated (-EBADF). they
 * are ABI: src/libc/include/errno.h has the same numbers. kernel code
 * returns kerr_t inside the kernel and converts at the syscall boundary
 * (kerr_to_errno); code that talks to user space directly (syscalls, ioctl
 * handlers) uses these names, never bare numbers.
 */

#define ENOSYS       1  // function not implemented
#define EBADF        2  // bad file descriptor
#define EINVAL       3  // invalid argument
#define ENOMEM       4  // not enough memory
#define ENOENT       5  // no such file or directory
#define EACCES       6  // permission denied
#define EPIPE        7  // broken pipe
#define ESPIPE       8  // invalid seek
#define EEXIST       9  // file exists
#define EISDIR       10 // is a directory
#define ENOTDIR      11 // not a directory
#define EMFILE       12 // too many open files
#define ENFILE       13 // too many open files in system
#define EIO          14 // i/o error
#define ENOSPC       15 // no space left on device
#define EBUSY        16 // resource busy
#define ERANGE       17 // result too large
#define EFAULT       18 // bad address
#define ESRCH        19 // no such process
#define ECHILD       20 // no child processes
#define ENOBUFS      21 // no buffer space available
#define ENOTTY       22 // inappropriate ioctl for device
#define ENODEV       23 // no such device
#define EINTR        24 // interrupted system call
#define EAGAIN       25 // resource temporarily unavailable
#define E2BIG        26 // argument list too long
#define ENOEXEC      27 // exec format error
#define ENOTEMPTY    28 // directory not empty
#define EXDEV        29 // cross-device link
#define ENAMETOOLONG 30 // file name too long
#define EPERM        31 // operation not permitted
#define ELOOP        32 // too many levels of symbolic links
#define EROFS        33 // read-only file system
#define ETIMEDOUT    34 // timed out
#define ENOTSOCK     35 // not a socket
#define EADDRINUSE   36 // address in use
#define ECONNREFUSED 37 // connection refused
#define EISCONN      38 // already connected
#define ENOTCONN     39 // not connected
#define EAFNOSUPPORT 40 // address family not supported
#define EPROTONOSUPPORT 41 // protocol not supported
#define EOPNOTSUPP   42 // operation not supported

#define EWOULDBLOCK EAGAIN

// kernel internal, never seen by user space: a sleeping syscall was
// interrupted by a signal. signal_deliver turns it into EINTR, or runs the
// syscall again (SA_RESTART, or when no handler runs).
#define ERESTARTSYS 512

/**
 * the errno for a kernel error, negated, for returning to user space.
 * @param err the kernel error.
 * @return 0 for KERR_OK, else a negative errno.
 */
static inline int32_t kerr_to_errno(kerr_t err)
{
    switch (err)
    {
    case KERR_OK:       return 0;
    case KERR_NOMEM:    return -ENOMEM;
    case KERR_IO:       return -EIO;
    case KERR_NOTFOUND: return -ENOENT;
    case KERR_PERM:     return -EACCES;
    case KERR_INVAL:    return -EINVAL;
    case KERR_BUSY:     return -EBUSY;
    case KERR_NOSPC:    return -ENOSPC;
    case KERR_RANGE:    return -ERANGE;
    case KERR_EXIST:    return -EEXIST;
    case KERR_ISDIR:    return -EISDIR;
    case KERR_NOTDIR:   return -ENOTDIR;
    case KERR_PIPE:     return -EPIPE;
    case KERR_NOENT:    return -ENOENT;
    case KERR_FAULT:    return -EFAULT;
    case KERR_NOTEMPTY: return -ENOTEMPTY;
    }
    return -EINVAL;
}

#include "errno.h"
#include "stdio.h"

int errno = 0;

const char *strerror(int errnum)
{
    switch (errnum)
    {
    case 0:       return "success";
    case ENOSYS:  return "function not implemented";
    case EBADF:   return "bad file descriptor";
    case EINVAL:  return "invalid argument";
    case ENOMEM:  return "out of memory";
    case ENOENT:  return "no such file or directory";
    case EACCES:  return "permission denied";
    case EPIPE:   return "broken pipe";
    case ESPIPE:  return "invalid seek";
    case EEXIST:  return "file exists";
    case EISDIR:  return "is a directory";
    case ENOTDIR: return "not a directory";
    case EMFILE:  return "too many open files";
    case ENFILE:  return "too many open files in system";
    case EIO:     return "i/o error";
    case ENOSPC:  return "no space left on device";
    case EBUSY:   return "resource busy";
    case ERANGE:  return "result too large";
    case EFAULT:  return "bad address";
    case ESRCH:   return "no such process";
    case ECHILD:  return "no child processes";
    case ENOBUFS: return "no buffer space available";
    case ENOTTY:  return "inappropriate ioctl for device";
    case ENODEV:  return "no such device";
    case EINTR:   return "interrupted system call";
    case EAGAIN:  return "resource temporarily unavailable";
    case E2BIG:   return "argument list too long";
    case ENOEXEC: return "exec format error";
    case ENOTEMPTY: return "directory not empty";
    case EXDEV:   return "cross-device link";
    case ENAMETOOLONG: return "file name too long";
    case EPERM:   return "operation not permitted";
    case ELOOP:   return "too many levels of symbolic links";
    case EROFS:   return "read-only file system";
    case ETIMEDOUT: return "timed out";
    case ENOTSOCK: return "not a socket";
    case EADDRINUSE: return "address in use";
    case ECONNREFUSED: return "connection refused";
    case EISCONN: return "already connected";
    case ENOTCONN: return "not connected";
    case EILSEQ:  return "invalid or incomplete multibyte character";
    case EMLINK:  return "too many links";
    case EFBIG:   return "file too large";
    case ENOLCK:  return "no locks available";
    case EAFNOSUPPORT: return "address family not supported";
    case EPROTONOSUPPORT: return "protocol not supported";
    case EOPNOTSUPP: return "operation not supported";
    case ECANCELED: return "cancelled";
    }
    return "unknown error";
}

void perror(const char *s)
{
    if (s && s[0] != '\0')
    {
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    }
    else
    {
        fprintf(stderr, "%s\n", strerror(errno));
    }
}

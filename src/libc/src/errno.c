#include "errno.h"
#include "stdio.h"

static __thread int thread_errno;

int *__errno_location(void)
{
    return &thread_errno;
}

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
    case ENXIO:   return "no such device or address";
    case EAFNOSUPPORT: return "address family not supported";
    case EPROTONOSUPPORT: return "protocol not supported";
    case EOPNOTSUPP: return "operation not supported";
    case ECANCELED: return "cancelled";
    case EDEADLK: return "resource deadlock would occur";
    case EOVERFLOW: return "value too large";
    case EADDRNOTAVAIL: return "address not available";
    case EDESTADDRREQ: return "destination address required";
    case EMSGSIZE: return "message too long";
    case ENETUNREACH: return "network unreachable";
    case EHOSTUNREACH: return "host unreachable";
    case ENOPROTOOPT: return "protocol not available";
    case EPROTOTYPE: return "wrong protocol type for socket";
    case ECONNRESET: return "connection reset by peer";
    case ECONNABORTED: return "connection aborted";
    case EINPROGRESS: return "operation in progress";
    case EALREADY: return "operation already in progress";
    case ENETDOWN: return "network is down";
    case EPROTO: return "protocol error";
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

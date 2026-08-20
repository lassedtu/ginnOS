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
    }
    return "unknown error";
}

void perror(const char *s)
{
    if (s && s[0] != '\0')
    {
        printf("%s: %s\n", s, strerror(errno));
    }
    else
    {
        printf("%s\n", strerror(errno));
    }
}

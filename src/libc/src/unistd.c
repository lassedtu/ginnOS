#include "unistd.h"
#include "errno.h"

/**
 * @file unistd.c
 * @brief This file contains the implementations of POSIX-like system call wrappers.
 */

ssize_t write(int fd, const void *buf, size_t count)
{
    int ret = _syscall(SYS_WRITE, fd, (int)buf, (int)count, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return (ssize_t)ret;
}

ssize_t read(int fd, void *buf, size_t count)
{
    int ret = _syscall(SYS_READ, fd, (int)buf, (int)count, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return (ssize_t)ret;
}

int open(const char *path, int flags)
{
    int ret = _syscall(SYS_OPEN, (int)path, flags, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int close(int fd)
{
    int ret = _syscall(SYS_CLOSE, fd, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

pid_t exec(const char *path, const char **argv)
{
    int ret = _syscall(SYS_EXEC, (int)path, (int)argv, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return (pid_t)ret;
}

int waitpid(pid_t pid)
{
    int ret = _syscall(SYS_WAITPID, (int)pid, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

void _exit(int status)
{
    _syscall(SYS_EXIT, status, 0, 0, 0, 0);
    __builtin_unreachable();
}

pid_t getpid(void)
{
    return (pid_t)_syscall(SYS_GETPID, 0, 0, 0, 0, 0);
}

void *sbrk(int increment)
{
    return (void *)_syscall(SYS_SBRK, increment, 0, 0, 0, 0);
}

int getcwd(char *buf, size_t size)
{
    int ret = _syscall(SYS_GETCWD, (int)buf, (int)size, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int chdir(const char *path)
{
    int ret = _syscall(SYS_CHDIR, (int)path, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int readdir(int fd, dirent_t *entry)
{
    int ret = _syscall(SYS_READDIR, fd, (int)entry, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int unlink(const char *path)
{
    int ret = _syscall(SYS_UNLINK, (int)path, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int rmdir(const char *path)
{
    int ret = _syscall(SYS_RMDIR, (int)path, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int create(const char *path)
{
    int ret = _syscall(SYS_CREATE, (int)path, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int mkdir(const char *path)
{
    int ret = _syscall(SYS_MKDIR, (int)path, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int ttyctl(int mode)
{
    int ret = _syscall(SYS_TTYCTL, mode, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int read_event(key_event_t *event)
{
    int n = (int)read(0, (void *)event, sizeof(key_event_t));
    if (n < (int)sizeof(key_event_t))
        return -1;
    return 0;
}

int pipe(int fds[2])
{
    int ret = _syscall(SYS_PIPE, (int)fds, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int dup2(int old_fd, int new_fd)
{
    int ret = _syscall(SYS_DUP2, old_fd, new_fd, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int ftruncate(int fd)
{
    int ret = _syscall(SYS_FTRUNCATE, fd, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int lseek(int fd, int offset, int whence)
{
    int ret = _syscall(SYS_LSEEK, fd, offset, whence, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

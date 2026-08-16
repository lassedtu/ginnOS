#include "../include/unistd.h"

/**
 * @file unistd.c
 * @brief This file contains the implementations of POSIX-like system call wrappers.
 */

ssize_t write(int fd, const void *buf, size_t count)
{
    return (ssize_t)_syscall(SYS_WRITE, fd, (int)buf, (int)count, 0, 0);
}

ssize_t read(int fd, void *buf, size_t count)
{
    return (ssize_t)_syscall(SYS_READ, fd, (int)buf, (int)count, 0, 0);
}

int open(const char *path, int flags)
{
    return _syscall(SYS_OPEN, (int)path, flags, 0, 0, 0);
}

int close(int fd)
{
    return _syscall(SYS_CLOSE, fd, 0, 0, 0, 0);
}

pid_t exec(const char *path, const char **argv)
{
    return (pid_t)_syscall(SYS_EXEC, (int)path, (int)argv, 0, 0, 0);
}

int waitpid(pid_t pid)
{
    return _syscall(SYS_WAITPID, (int)pid, 0, 0, 0, 0);
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
    return _syscall(SYS_GETCWD, (int)buf, (int)size, 0, 0, 0);
}

int chdir(const char *path)
{
    return _syscall(SYS_CHDIR, (int)path, 0, 0, 0, 0);
}

int readdir(int fd, dirent_t *entry)
{
    return _syscall(SYS_READDIR, fd, (int)entry, 0, 0, 0);
}

int unlink(const char *path)
{
    return _syscall(SYS_UNLINK, (int)path, 0, 0, 0, 0);
}

int rmdir(const char *path)
{
    return _syscall(SYS_RMDIR, (int)path, 0, 0, 0, 0);
}

int create(const char *path)
{
    return _syscall(SYS_CREATE, (int)path, 0, 0, 0, 0);
}

int mkdir(const char *path)
{
    return _syscall(SYS_MKDIR, (int)path, 0, 0, 0, 0);
}

int clear_screen(void)
{
    return _syscall(SYS_CLEAR, 0, 0, 0, 0, 0);
}

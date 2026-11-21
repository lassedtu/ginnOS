#include "unistd.h"
#include <stdarg.h>
#include "errno.h"
#include <sys/stat.h>
#include <stdio.h>

/**
 * @file unistd.c
 * @brief This file contains the implementations of POSIX-like system call wrappers.
 */

ssize_t write(int fd, const void *buf, size_t count)
{
    int ret = _syscall(SYS_WRITE, fd, (long)buf, (long)count, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return (ssize_t)ret;
}

ssize_t read(int fd, void *buf, size_t count)
{
    int ret = _syscall(SYS_READ, fd, (long)buf, (long)count, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return (ssize_t)ret;
}

int open(const char *path, int flags, ...)
{
    // the mode is only there with O_CREAT; read it as the next argument
    va_list args;
    va_start(args, flags);
    mode_t mode = (flags & O_CREAT) ? va_arg(args, mode_t) : 0;
    va_end(args);

    int ret = _syscall(SYS_OPEN, (long)path, flags, (long)mode, 0, 0);
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
    int ret = _syscall(SYS_EXEC, (long)path, (long)argv, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return (pid_t)ret;
}

pid_t fork(void)
{
    int ret = _syscall(SYS_FORK, 0, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return (pid_t)ret;
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    int ret = _syscall(SYS_EXECVE, (long)path, (long)argv, (long)envp, 0, 0);
    errno = -ret; // execve only returns on failure
    return -1;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    int ret = _syscall(SYS_WAITPID, (long)pid, (long)status, options, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return (pid_t)ret;
}

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
}

pid_t getppid(void)
{
    return (pid_t)_syscall(SYS_GETPPID, 0, 0, 0, 0, 0);
}

int sched_yield(void)
{
    return _syscall(SYS_SCHED_YIELD, 0, 0, 0, 0, 0);
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
    int ret = _syscall(SYS_GETCWD, (long)buf, (long)size, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int chdir(const char *path)
{
    int ret = _syscall(SYS_CHDIR, (long)path, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int unlink(const char *path)
{
    int ret = _syscall(SYS_UNLINK, (long)path, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int rmdir(const char *path)
{
    int ret = _syscall(SYS_RMDIR, (long)path, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int creat(const char *path, mode_t mode)
{
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int create(const char *path)
{
    int fd = creat(path, 0644);
    if (fd < 0)
    {
        return -1;
    }
    close(fd);
    return 0;
}

int mkdir(const char *path, mode_t mode)
{
    int ret = _syscall(SYS_MKDIR, (long)path, (long)mode, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int ioctl(int fd, unsigned int request, void *arg)
{
    int ret = _syscall(SYS_IOCTL, fd, (long)request, (long)arg, 0, 0);
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
    int ret = _syscall(SYS_PIPE, (long)fds, 0, 0, 0, 0);
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

/**
 * the usual syscall wrapper tail: a negative result sets errno.
 */
static int check(int ret)
{
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int fcntl(int fd, int cmd, ...)
{
    va_list args;
    va_start(args, cmd);
    int arg = va_arg(args, int);
    va_end(args);
    return check(_syscall(SYS_FCNTL, fd, cmd, arg, 0, 0));
}

int dup(int fd)
{
    return fcntl(fd, F_DUPFD, 0);
}

int stat(const char *path, struct stat *st)
{
    return check(_syscall(SYS_STAT, (long)path, (long)st, 0, 0, 0));
}

int fstat(int fd, struct stat *st)
{
    return check(_syscall(SYS_FSTAT, fd, (long)st, 0, 0, 0));
}

int access(const char *path, int mode)
{
    return check(_syscall(SYS_ACCESS, (long)path, mode, 0, 0, 0));
}

int rename(const char *old_path, const char *new_path)
{
    return check(_syscall(SYS_RENAME, (long)old_path, (long)new_path, 0, 0, 0));
}

ssize_t pread(int fd, void *buf, size_t count, int offset)
{
    return check(_syscall(SYS_PREAD, fd, (long)buf, (long)count, offset, 0));
}

ssize_t pwrite(int fd, const void *buf, size_t count, int offset)
{
    return check(_syscall(SYS_PWRITE, fd, (long)buf, (long)count, offset, 0));
}

int fsync(int fd)
{
    return check(_syscall(SYS_FSYNC, fd, 0, 0, 0, 0));
}

int setpgid(pid_t pid, pid_t pgid)
{
    return check(_syscall(SYS_SETPGID, pid, pgid, 0, 0, 0));
}

pid_t getpgid(pid_t pid)
{
    return check(_syscall(SYS_GETPGID, pid, 0, 0, 0, 0));
}

pid_t getpgrp(void)
{
    return getpgid(0);
}

pid_t setsid(void)
{
    return check(_syscall(SYS_SETSID, 0, 0, 0, 0, 0));
}

pid_t getsid(pid_t pid)
{
    return check(_syscall(SYS_GETSID, pid, 0, 0, 0, 0));
}

uid_t getuid(void)
{
    return (uid_t)_syscall(SYS_GETUID, 0, 0, 0, 0, 0);
}

uid_t geteuid(void)
{
    return (uid_t)_syscall(SYS_GETEUID, 0, 0, 0, 0, 0);
}

gid_t getgid(void)
{
    return (gid_t)_syscall(SYS_GETGID, 0, 0, 0, 0, 0);
}

gid_t getegid(void)
{
    return (gid_t)_syscall(SYS_GETEGID, 0, 0, 0, 0, 0);
}

int setuid(uid_t uid)
{
    return check(_syscall(SYS_SETUID, (long)uid, 0, 0, 0, 0));
}

int setgid(gid_t gid)
{
    return check(_syscall(SYS_SETGID, (long)gid, 0, 0, 0, 0));
}

int seteuid(uid_t uid)
{
    return check(_syscall(SYS_SETEUID, (long)uid, 0, 0, 0, 0));
}

int setegid(gid_t gid)
{
    return check(_syscall(SYS_SETEGID, (long)gid, 0, 0, 0, 0));
}

int chmod(const char *path, mode_t mode)
{
    return check(_syscall(SYS_CHMOD, (long)path, (long)mode, 0, 0, 0));
}

int fchmod(int fd, mode_t mode)
{
    return check(_syscall(SYS_FCHMOD, fd, (long)mode, 0, 0, 0));
}

int chown(const char *path, uid_t uid, gid_t gid)
{
    return check(_syscall(SYS_CHOWN, (long)path, (long)uid, (long)gid, 0, 0));
}

mode_t umask(mode_t mask)
{
    return (mode_t)_syscall(SYS_UMASK, (long)mask, 0, 0, 0, 0);
}

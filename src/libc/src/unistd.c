#include "unistd.h"
#include "sys/time.h"
#include "utime.h"
#include <stdarg.h>
#include <stdbool.h>
#include "errno.h"
#include <dirent.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/reboot.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/utsname.h>
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

int execv(const char *path, char *const argv[])
{
    return execve(path, argv, environ);
}

int execvp(const char *file, char *const argv[])
{
    if (!file || !*file)
    {
        errno = ENOENT;
        return -1;
    }
    if (strchr(file, '/'))
        return execv(file, argv);

    const char *path = getenv("PATH");
    if (!path || !*path)
        path = "/bin";
    size_t name_length = strlen(file);
    int error = ENOENT;
    while (*path)
    {
        const char *end = strchr(path, ':');
        size_t length = end ? (size_t)(end - path) : strlen(path);
        char full[PATH_MAX];
        if (length + 1 + name_length < sizeof(full))
        {
            // an empty entry is the current directory
            size_t at = 0;
            if (length == 0)
                full[at++] = '.';
            memcpy(full + at, path, length);
            at += length;
            full[at++] = '/';
            memcpy(full + at, file, name_length + 1);
            execv(full, argv);
            // go on when it is not there; remember that one was found
            if (errno != ENOENT && errno != ENOTDIR)
                error = errno;
        }
        path += length;
        if (*path == ':')
            path++;
    }
    errno = error;
    return -1;
}

#define EXECL_ARGS_MAX 64

/**
 * the arguments of an execl call (arg, then the va_list up to NULL) as an
 * argv array.
 * @return false if there are too many.
 */
static bool list_to_argv(const char *arg, va_list *ap, char *argv[EXECL_ARGS_MAX])
{
    int count = 0;
    for (const char *a = arg; a; a = va_arg(*ap, const char *))
    {
        if (count == EXECL_ARGS_MAX - 1)
            return false;
        argv[count++] = (char *)a;
    }
    argv[count] = NULL;
    return true;
}

int execl(const char *path, const char *arg, ...)
{
    char *argv[EXECL_ARGS_MAX];
    va_list ap;
    va_start(ap, arg);
    bool fits = list_to_argv(arg, &ap, argv);
    va_end(ap);
    if (!fits)
    {
        errno = E2BIG;
        return -1;
    }
    return execv(path, argv);
}

int execlp(const char *file, const char *arg, ...)
{
    char *argv[EXECL_ARGS_MAX];
    va_list ap;
    va_start(ap, arg);
    bool fits = list_to_argv(arg, &ap, argv);
    va_end(ap);
    if (!fits)
    {
        errno = E2BIG;
        return -1;
    }
    return execvp(file, argv);
}

int execle(const char *path, const char *arg, ...)
{
    char *argv[EXECL_ARGS_MAX];
    va_list ap;
    va_start(ap, arg);
    bool fits = list_to_argv(arg, &ap, argv);
    char *const *envp = va_arg(ap, char *const *); // after the NULL
    va_end(ap);
    if (!fits)
    {
        errno = E2BIG;
        return -1;
    }
    return execve(path, argv, envp);
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
    _syscall(SYS_EXIT_GROUP, status, 0, 0, 0, 0); // every thread
    __builtin_unreachable();
}

void _Exit(int status)
{
    _exit(status);
}

pid_t getpid(void)
{
    return (pid_t)_syscall(SYS_GETPID, 0, 0, 0, 0, 0);
}

void *sbrk(int increment)
{
    return (void *)_syscall(SYS_SBRK, increment, 0, 0, 0, 0);
}

char *getcwd(char *buf, size_t size)
{
    char own[PATH_MAX];
    char *out = buf ? buf : own;
    size_t room = buf ? size : sizeof(own);
    int ret = _syscall(SYS_GETCWD, (long)out, (long)room, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return NULL;
    }
    if (buf)
        return buf;
    size_t length = strlen(own) + 1;
    if (size && size < length)
    {
        errno = ERANGE;
        return NULL;
    }
    char *copy = malloc(size ? size : length);
    if (!copy)
    {
        errno = ENOMEM;
        return NULL;
    }
    return memcpy(copy, own, length);
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

int ftruncate(int fd, off_t length)
{
    int ret = _syscall(SYS_FTRUNCATE, fd, (long)length, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return 0;
}

int truncate(const char *path, off_t length)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;
    int ret = ftruncate(fd, length);
    int saved = errno;
    close(fd);
    errno = saved;
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

int lstat(const char *path, struct stat *st)
{
    return fstatat(AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW);
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

int reboot(int how)
{
    return check(_syscall(SYS_REBOOT, how, 0, 0, 0, 0));
}

int uname(struct utsname *name)
{
    return check(_syscall(SYS_UNAME, (long)name, 0, 0, 0, 0));
}

long sysconf(int name)
{
    switch (name)
    {
    case _SC_ARG_MAX:
        return ARG_MAX;
    case _SC_CHILD_MAX:
        return 64; // the kernel's PROCESS_MAX
    case _SC_CLK_TCK:
        return 100; // times() counts in hundredths of a second
    case _SC_OPEN_MAX:
        return 64; // the kernel's FD_MAX
    case _SC_PAGESIZE:
        return 4096;
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN:
        return 1;
    case _SC_LINE_MAX:
        return LINE_MAX;
    case _SC_HOST_NAME_MAX:
        return HOST_NAME_MAX;
    case _SC_LOGIN_NAME_MAX:
        return 32;
    default:
        errno = EINVAL;
        return -1;
    }
}

int gethostname(char *name, size_t length)
{
    struct utsname u;
    if (uname(&u) < 0)
        return -1;
    if (strlen(u.nodename) >= length)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(name, u.nodename);
    return 0;
}

char *getlogin(void)
{
    static char name[33];
    struct passwd *pw = getpwuid(getuid());
    if (!pw)
        return NULL;
    snprintf(name, sizeof(name), "%s", pw->pw_name);
    return name;
}

int openat(int dirfd, const char *path, int flags, ...)
{
    va_list args;
    va_start(args, flags);
    mode_t mode = (flags & O_CREAT) ? va_arg(args, mode_t) : 0;
    va_end(args);
    return check(_syscall(SYS_OPENAT, dirfd, (long)path, flags, (long)mode, 0));
}

int mkdirat(int dirfd, const char *path, mode_t mode)
{
    return check(_syscall(SYS_MKDIRAT, dirfd, (long)path, (long)mode, 0, 0));
}

int unlinkat(int dirfd, const char *path, int flags)
{
    return check(_syscall(SYS_UNLINKAT, dirfd, (long)path, flags, 0, 0));
}

int renameat(int old_dirfd, const char *old_path, int new_dirfd, const char *new_path)
{
    return check(_syscall(SYS_RENAMEAT, old_dirfd, (long)old_path, new_dirfd, (long)new_path, 0));
}

int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    return check(_syscall(SYS_FSTATAT, dirfd, (long)path, (long)st, flags, 0));
}

int faccessat(int dirfd, const char *path, int mode, int flags)
{
    return check(_syscall(SYS_FACCESSAT, dirfd, (long)path, mode, flags, 0));
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    return check(_syscall(SYS_FCHMODAT, dirfd, (long)path, (long)mode, flags, 0));
}

int fchownat(int dirfd, const char *path, uid_t uid, gid_t gid, int flags)
{
    return check(_syscall(SYS_FCHOWNAT, dirfd, (long)path, (long)uid, (long)gid, flags));
}

int fchown(int fd, uid_t uid, gid_t gid)
{
    return check(_syscall(SYS_FCHOWN, fd, (long)uid, (long)gid, 0, 0));
}

int lchown(const char *path, uid_t uid, gid_t gid)
{
    return fchownat(AT_FDCWD, path, uid, gid, AT_SYMLINK_NOFOLLOW);
}

int symlinkat(const char *target, int dirfd, const char *path)
{
    return check(_syscall(SYS_SYMLINKAT, (long)target, dirfd, (long)path, 0, 0));
}

int symlink(const char *target, const char *path)
{
    return symlinkat(target, AT_FDCWD, path);
}

ssize_t readlinkat(int dirfd, const char *restrict path, char *restrict buffer, size_t size)
{
    return check(_syscall(SYS_READLINKAT, dirfd, (long)path, (long)buffer, (long)size, 0));
}

ssize_t readlink(const char *restrict path, char *restrict buffer, size_t size)
{
    return readlinkat(AT_FDCWD, path, buffer, size);
}

int linkat(int old_dirfd, const char *old_path, int new_dirfd, const char *new_path, int flags)
{
    return check(_syscall(SYS_LINKAT, old_dirfd, (long)old_path, new_dirfd, (long)new_path, flags));
}

int link(const char *old_path, const char *new_path)
{
    return linkat(AT_FDCWD, old_path, AT_FDCWD, new_path, 0);
}

int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
    // a NULL path would make the kernel act on dirfd: that is futimens
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    return check(_syscall(SYS_UTIMENSAT, dirfd, (long)path, (long)times, flags, 0));
}

int futimens(int fd, const struct timespec times[2])
{
    return check(_syscall(SYS_UTIMENSAT, fd, 0, (long)times, 0, 0));
}

int utime(const char *path, const struct utimbuf *times)
{
    if (!times)
        return utimensat(AT_FDCWD, path, NULL, 0);
    struct timespec both[2] = {{times->actime, 0}, {times->modtime, 0}};
    return utimensat(AT_FDCWD, path, both, 0);
}

int utimes(const char *path, const struct timeval times[2])
{
    if (!times)
        return utimensat(AT_FDCWD, path, NULL, 0);
    struct timespec both[2] = {{times[0].tv_sec, times[0].tv_usec * 1000},
                               {times[1].tv_sec, times[1].tv_usec * 1000}};
    return utimensat(AT_FDCWD, path, both, 0);
}

int clock_settime(clockid_t id, const struct timespec *when)
{
    return check(_syscall(SYS_CLOCK_SETTIME, id, (long)when, 0, 0, 0));
}

int sethostname(const char *name, size_t length)
{
    return check(_syscall(SYS_SETHOSTNAME, (long)name, (long)length, 0, 0, 0));
}

void sync(void)
{
    // the block cache writes through: the disk is never behind
}

int daemon(int keep_dir, int keep_files)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(0);
    if (setsid() < 0)
        return -1;
    if (!keep_dir && chdir("/") < 0)
        return -1;
    if (!keep_files)
    {
        int fd = open("/dev/null", O_RDWR);
        if (fd < 0)
            return -1;
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2)
            close(fd);
    }
    return 0;
}

/** find the character device numbered rdev in dir; its path into buffer */
static int find_device(const char *dir, dev_t rdev, char *buffer, size_t size)
{
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    struct dirent *entry;
    int found = -1;
    while (found < 0 && (entry = readdir(d)))
    {
        char path[64];
        struct stat st;
        if (entry->d_name[0] == '.')
            continue;
        if (snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name) >= (int)sizeof(path))
            continue;
        if (stat(path, &st) == 0 && S_ISCHR(st.st_mode) && st.st_rdev == rdev)
        {
            found = strlen(path) < size ? 0 : ERANGE;
            if (found == 0)
                strcpy(buffer, path);
        }
    }
    closedir(d);
    return found;
}

int ttyname_r(int fd, char *buffer, size_t size)
{
    struct stat st;
    if (fstat(fd, &st) < 0)
        return errno;
    if (!S_ISCHR(st.st_mode) || st.st_rdev == 0 || !isatty(fd))
        return ENOTTY;
    int found = find_device("/dev/pts", st.st_rdev, buffer, size);
    if (found < 0)
        found = find_device("/dev", st.st_rdev, buffer, size);
    return found < 0 ? ENOTTY : found;
}

char *ttyname(int fd)
{
    static char name[64];
    int err = ttyname_r(fd, name, sizeof(name));
    if (err)
    {
        errno = err;
        return NULL;
    }
    return name;
}

int getpriority(int which, id_t who)
{
    int ret = _syscall(SYS_GETPRIORITY, which, (long)who, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return 20 - ret; // the kernel gives 20 - nice, never negative
}

int setpriority(int which, id_t who, int value)
{
    return check(_syscall(SYS_SETPRIORITY, which, (long)who, value, 0, 0));
}

int nice(int increment)
{
    errno = 0;
    int now = getpriority(PRIO_PROCESS, 0);
    if (now == -1 && errno)
        return -1;
    if (setpriority(PRIO_PROCESS, 0, now + increment) < 0)
    {
        if (errno == EACCES)
            errno = EPERM;
        return -1;
    }
    return getpriority(PRIO_PROCESS, 0);
}

int flock(int fd, int operation)
{
    return check(_syscall(SYS_FLOCK, fd, operation, 0, 0, 0));
}

int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
    return check(_syscall(SYS_MKNODAT, dirfd, (long)path, (long)mode, (long)dev, 0));
}

int mknod(const char *path, mode_t mode, dev_t dev)
{
    return mknodat(AT_FDCWD, path, mode, dev);
}

int mkfifoat(int dirfd, const char *path, mode_t mode)
{
    return mknodat(dirfd, path, S_IFIFO | (mode & 07777), 0);
}

int mkfifo(const char *path, mode_t mode)
{
    return mkfifoat(AT_FDCWD, path, mode);
}

int chroot(const char *path)
{
    return check(_syscall(SYS_CHROOT, (long)path, 0, 0, 0, 0));
}

/** a _PC_ limit; the file must exist (checked by the caller) */
static long path_limit(int name)
{
    switch (name)
    {
    case _PC_LINK_MAX:
        return 32000; // ext2's links to one file (the kernel's EXT2_LINK_MAX)
    case _PC_MAX_CANON:
    case _PC_MAX_INPUT:
        return 255; // a terminal's line
    case _PC_NAME_MAX:
        return NAME_MAX;
    case _PC_PATH_MAX:
        return PATH_MAX;
    case _PC_PIPE_BUF:
        return PIPE_BUF;
    case _PC_CHOWN_RESTRICTED:
    case _PC_NO_TRUNC:
        return 1; // only root gives files away; a long name is an error
    case _PC_VDISABLE:
        return 0; // a terminal control character of 0 is off
    default:
        errno = EINVAL;
        return -1;
    }
}

long pathconf(const char *path, int name)
{
    struct stat st;
    if (stat(path, &st) < 0)
        return -1;
    return path_limit(name);
}

long fpathconf(int fd, int name)
{
    struct stat st;
    if (fstat(fd, &st) < 0)
        return -1;
    return path_limit(name);
}

size_t confstr(int name, char *buffer, size_t size)
{
    if (name != _CS_PATH)
    {
        errno = EINVAL;
        return 0;
    }
    const char *value = "/bin";
    size_t length = strlen(value) + 1;
    if (buffer && size)
    {
        size_t copy = length < size ? length : size;
        memcpy(buffer, value, copy - 1);
        buffer[copy - 1] = '\0';
    }
    return length;
}

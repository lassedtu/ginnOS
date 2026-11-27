<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;unistd.h&gt;

Source: [`src/libc/include/unistd.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/unistd.h)

This file contains the declarations of POSIX-like system call wrappers.

## write

```c
ssize_t write(int fd, const void *buf, size_t count);
```

write bytes to a file descriptor.

- `fd`: file descriptor (1 = stdout, 2 = stderr).
- `buf`: pointer to data to write.
- `count`: number of bytes to write.

Returns: number of bytes written, or -1 on error.

## read

```c
ssize_t read(int fd, void *buf, size_t count);
```

read bytes from a file descriptor.

- `fd`: file descriptor (0 = stdin).
- `buf`: buffer to read into.
- `count`: maximum bytes to read.

Returns: number of bytes read, or -1 on error.

## close

```c
int close(int fd);
```

close a file descriptor.

- `fd`: file descriptor to close.

Returns: 0 on success, -1 on failure.

## exec

```c
pid_t exec(const char *path, const char **argv);
```

execute a program, spawning a child process.

- `path`: path to the ELF executable.
- `argv`: null-terminated array of argument strings (may be NULL).

Returns: child PID on success, -1 on failure.

## fork

```c
pid_t fork(void);
```

create a child process that is a copy of the caller. the two share
nothing after the call: memory is copied (lazily, on the first write),
and the child gets its own copy of every fd.

Returns: the child's PID in the parent, 0 in the child, -1 on failure (errno set).

## execve

```c
int execve(const char *path, char *const argv[], char *const envp[]);
```

replace the calling process's program. the pid, cwd and open fds stay.

- `path`: path to the ELF executable.
- `argv`: NULL-terminated argument strings.
- `envp`: NULL-terminated "KEY=VALUE" environment strings.

Returns: only on failure: -1 (errno set).

## execv

```c
int execv(const char *path, char *const argv[]);
```

execve with the environment of the calling process.

Returns: only on failure: -1 (errno set).

## execvp

```c
int execvp(const char *file, char *const argv[]);
```

execv, but a file name with no '/' is looked for in the directories of
PATH (/bin when PATH is not set).

Returns: only on failure: -1 (errno set).

## execl, execlp, execle

```c
int execl(const char *path, const char *arg, ...);
int execlp(const char *file, const char *arg, ...);
int execle(const char *path, const char *arg, ...);
```

execv, execvp and execve with the arguments as a list that ends with
NULL. execle takes the environment after the NULL.

Returns: only on failure: -1 (errno set).

## \_exit

```c
void _exit(int status) __attribute__((noreturn));
```

terminate the current process.

- `status`: exit code.

## dup

```c
int dup(int fd);
```

duplicate a descriptor to the lowest free one; both share the open file.

Returns: the new descriptor, or -1 (errno set).

## access, faccessat

```c
int access(const char *path, int mode);
int faccessat(int dirfd, const char *path, int mode, int flags);
```

check that a file exists and may be accessed. there are no users yet,
so every existing file allows every access.

- `path`: the file.
- `mode`: F\_OK, or R\_OK | W\_OK | X\_OK.

Returns: 0, or -1 (errno set).

## pread, pwrite

```c
ssize_t pread(int fd, void *buf, size_t count, int offset);
ssize_t pwrite(int fd, const void *buf, size_t count, int offset);
```

read or write at an offset, without moving the file offset.

Returns: bytes transferred, or -1 (errno set; ESPIPE for a pipe).

## fsync

```c
int fsync(int fd);
```

write an open file's data to disk.

Returns: 0, or -1 (errno set).

## alarm

```c
unsigned int alarm(unsigned int seconds);
```

send SIGALRM to the calling process after a number of seconds.

- `seconds`: 0 cancels the alarm.

Returns: the seconds left of the previous alarm, or 0.

## pause

```c
int pause(void);
```

sleep until a signal is caught. always returns -1 with EINTR.

## sleep, usleep

```c
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int microseconds);
```

sleep for a number of seconds (or microseconds).

Returns: sleep: the seconds left if a signal ended it early, else 0. usleep: 0, or -1 (errno set).

## setpgid, getpgid, getpgrp, setsid

```c
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgid(pid_t pid);
pid_t getpgrp(void);
pid_t setsid(void);
```

process groups and sessions (job control).
setpgid(pid, pgid): pid 0 is the caller, pgid 0 makes a new group led
by pid. getpgrp() is getpgid(0). setsid() starts a session and returns
its id.

Returns: as named, or -1 (errno set).

## nice

```c
int nice(int increment);
```

add increment to the caller's nice value (see sys/resource.h).

Returns: the new value, or -1 (errno set).

## chroot, getsid

```c
int chroot(const char *path);
pid_t getsid(pid_t pid);
```

make a directory the root of the caller's paths ("/" is then that
directory; root only). the working directory stays: chdir("/") after.

Returns: 0, or -1 (errno set).

## getuid, geteuid, getgid, getegid, setuid, setgid, seteuid, setegid

```c
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
int seteuid(uid_t uid);
int setegid(gid_t gid);
```

user and group ids: real (who started the process) and effective (what
it may do). setuid/setgid as root set all ids; otherwise only to the
real or saved id. set the group before giving up root.

## chown, fchown, lchown, fchownat

```c
int chown(const char *path, uid_t uid, gid_t gid);
int fchown(int fd, uid_t uid, gid_t gid);
int lchown(const char *path, uid_t uid, gid_t gid);
int fchownat(int dirfd, const char *path, uid_t uid, gid_t gid, int flags);
```

change the owner of a file. root may give a file to anyone; an owner may
only change its group to its own. (uid\_t)-1 or (gid\_t)-1 keeps one.

## getppid

```c
pid_t getppid(void);
```

get the parent's process ID.

Returns: the parent's PID, or 0 if the parent has exited.

## sched\_yield

```c
int sched_yield(void);
```

let other ready processes run before this one continues.

Returns: 0.

## getpid

```c
pid_t getpid(void);
```

get the current process ID.

Returns: the current PID.

## sbrk

```c
void *sbrk(int increment);
```

grow the program break (allocate heap memory).

- `increment`: number of bytes to grow by.

Returns: pointer to the previous break, or (void\*)-1 on failure.

## getcwd

```c
char *getcwd(char *buf, size_t size);
```

get the current working directory.

- `buf`: buffer to write the path into; NULL: a new one from malloc (of size bytes, or as long as needed when size is 0).
- `size`: size of the buffer.

Returns: buf (or the new buffer), or NULL (errno: ERANGE, ENOMEM).

## chdir

```c
int chdir(const char *path);
```

change the current working directory.

- `path`: path to change to.

Returns: 0 on success, -1 on failure.

## unlink

```c
int unlink(const char *path);
```

remove (unlink) a file.

- `path`: path to the file to remove.

Returns: 0 on success, -1 on failure.

## symlink, symlinkat

```c
int symlink(const char *target, const char *path);
int symlinkat(const char *target, int dirfd, const char *path);
```

make a symbolic link at path that points at target

## readlink, readlinkat

```c
ssize_t readlink(const char *restrict path, char *restrict buffer, size_t size);
ssize_t readlinkat(int dirfd, const char *restrict path, char *restrict buffer, size_t size);
```

read a symbolic link's target (no NUL; a longer target is cut).

Returns: the bytes put in buffer, or -1 (EINVAL: path is not a link).

## link, linkat

```c
int link(const char *old_path, const char *new_path);
int linkat(int old_dirfd, const char *old_path, int new_dirfd, const char *new_path, int flags);
```

give a file one more name (a hard link)

## unlinkat

```c
int unlinkat(int dirfd, const char *path, int flags);
```

remove a file, or with AT\_REMOVEDIR a directory, relative to dirfd

## rmdir

```c
int rmdir(const char *path);
```

remove a directory.

- `path`: path to the directory to remove.

Returns: 0 on success, -1 on failure.

## create

```c
int create(const char *path);
```

create a file, or truncate it if it exists (the file is not left open).

- `path`: path to the file to create.

Returns: 0 on success, -1 on failure.

## ttyctl

```c
int ttyctl(int mode);
```

switch the terminal on fd 0 between two modes (a wrapper over termios):
TTY\_COOKED is line input with echo and signal keys; TTY\_RAW gives one
key\_event\_t per read (read\_event), with no echo and Ctrl+C as a key.
the mode belongs to the terminal, so it stays after the program exits.

- `mode`: TTY\_COOKED or TTY\_RAW.

Returns: the previous mode, or -1 on error.

## getpass

```c
char *getpass(const char *prompt);
```

ask for a password on the terminal (standard input), without echo.

Returns: the line without its newline, in a static buffer (up to 127 bytes), or NULL at the end of input.

## isatty

```c
int isatty(int fd);
```

true if fd refers to a terminal.

## ioctl

```c
int ioctl(int fd, unsigned int request, void *arg);
```

issue a device-control request on an open file descriptor.
only device nodes under /dev service ioctls; other fds return -1/ENOTTY.

- `fd`: open file descriptor (e.g. an open "/dev/fb0").
- `request`: driver-defined request code.
- `arg`: request-specific argument, usually a pointer to a struct.

Returns: 0 (or a request-specific value) on success, -1 on error (errno set).

## key\_event\_t

```c
typedef struct
{
    int type;          // CONSOLE_KEY_EVENT_CHAR or CONSOLE_KEY_EVENT_SPECIAL
    union
    {
        char character;  // valid when type == CONSOLE_KEY_EVENT_CHAR
        int  special;    // valid when type == CONSOLE_KEY_EVENT_SPECIAL
        struct
        {
            unsigned char code;    // PS/2 set 1 scan code; bit 7: an 0xE0 key
            unsigned char pressed; // 1: down, 0: up
        } raw;                     // valid when type == CONSOLE_KEY_EVENT_RAW
    };
} key_event_t;
```

keyboard event structure (matches kernel keyboard\_event\_t).
in raw mode, read() on stdin fills this struct.

## read\_event

```c
int read_event(key_event_t *event);
```

read a single keyboard event from stdin (raw mode).
blocks until an event is available.

- `event`: pointer to event struct to fill.

Returns: 0 on success, -1 on error.

## pipe

```c
int pipe(int fds[2]);
```

create a pipe (pair of connected file descriptors).

- `fds`: array of two ints: fds\[0] = read end, fds\[1] = write end.

Returns: 0 on success, -1 on failure.

## dup2

```c
int dup2(int old_fd, int new_fd);
```

duplicate a file descriptor to a specific number.
if new\_fd is already open, it is closed first.

- `old_fd`: the fd to duplicate.
- `new_fd`: the target fd number.

Returns: new\_fd on success, -1 on failure.

## ftruncate, truncate

```c
int ftruncate(int fd, off_t length);
int truncate(const char *path, off_t length);
```

cut or grow a file to length bytes (the new bytes read as zeros). the
file offset stays.

Returns: 0, or -1 (errno set).

## lseek

```c
int lseek(int fd, int offset, int whence);
```

set file cursor position.

- `fd`: file descriptor.
- `offset`: byte offset.
- `whence`: SEEK\_SET (from start), SEEK\_CUR (from current), SEEK\_END (from end).

Returns: new cursor position, or -1 on error.

## sysconf

```c
long sysconf(int name);
```

a limit of the system (a \_SC\_ name), or -1 with EINVAL

## pathconf, fpathconf

```c
long pathconf(const char *path, int name);
long fpathconf(int fd, int name);
```

a limit of the files at path (a \_PC\_ name). ginnOS's limits are the
same on every filesystem.

Returns: the limit, or -1 (errno: EINVAL for a name, or as stat sets it).

## confstr

```c
size_t confstr(int name, char *buffer, size_t size);
```

a string value of the system (a \_CS\_ name) into buffer.

Returns: the size the whole value needs, NUL included; 0 with EINVAL for an unknown name.

## gethostname

```c
int gethostname(char *name, size_t length);
```

the name of this computer (uname's nodename) into name

## sethostname

```c
int sethostname(const char *name, size_t length);
```

set the host name (root only; at most 64 bytes)

## ttyname, ttyname\_r

```c
char *ttyname(int fd);
int ttyname_r(int fd, char *buffer, size_t size);
```

the name of the terminal open as fd (as "/dev/pts/0").

Returns: the name in a static buffer, or NULL (errno: ENOTTY, EBADF).

## sync

```c
void sync(void);
```

make every write reach the disk. ginnOS writes through its cache at
once, so there is nothing to wait for.

## daemon

```c
int daemon(int keep_dir, int keep_files);
```

go on in the background: a new process in a new session, with no
terminal.

- `keep_dir`: 0: change to "/".
- `keep_files`: 0: read and write /dev/null on 0, 1 and 2.

Returns: 0 in the new process, or -1 (errno set).

## getopt, optarg, optind, opterr, optopt

```c
int getopt(int argc, char *const argv[], const char *options);
extern char *optarg; // the argument of the option getopt returned
extern int optind;   // the next argument getopt looks at (0 starts again)
extern int opterr;   // 0: getopt writes no messages
extern int optopt;   // the option letter of the last error
```

the next option of a command line (POSIX getopt).

- `options`: the option letters; a letter with ':' after it takes an argument. a ':' first: no messages, and ':' for a missing argument.

Returns: the option letter, '?' for an unknown one (in optopt), or -1 after the last option (optind is then the first operand).

## getentropy

```c
int getentropy(void *buf, size_t length);
```

fill buf with length random bytes (at most 256) from the kernel.

Returns: 0, or -1 (errno EIO when length is more than 256).

## getlogin

```c
char *getlogin(void);
```

the name of the user who runs the program (from the password file), or NULL

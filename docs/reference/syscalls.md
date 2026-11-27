<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# The system calls

A program puts the number in EAX (RAX on x86_64) and the arguments
in EBX, ECX, EDX, ESI and EDI, then runs `int 0x80` (i686) or
`syscall` (x86_64). A negative result is an error number. The numbers
are in [`src/kernel/syscall/syscall.h`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/syscall.h).

| Number | Name | What it does |
| --- | --- | --- |
| 0 | [`exit`](#exit) | terminate the current process |
| 1 | [`write`](#write) | write bytes to a file descriptor |
| 2 | [`read`](#read) | read bytes from a file descriptor |
| 3 | [`open`](#open) |  |
| 4 | [`close`](#close) | close an open file descriptor |
| 5 | [`stat`](#stat) | get file metadata |
| 6 | `create` | retired: create() is open(O\_CREAT \| O\_WRONLY \| O\_TRUNC) |
| 7 | [`mkdir`](#mkdir) | create a directory |
| 8 | [`exec`](#exec) | start an ELF binary as a child process (spawn, not execve) |
| 9 | [`getpid`](#getpid) | return the current process ID (the thread group's id) |
| 10 | [`waitpid`](#waitpid) | wait for a child process to exit and reap it |
| 11 | [`sbrk`](#sbrk) |  |
| 12 | [`getcwd`](#getcwd) | copy the current working directory into a user buffer |
| 13 | [`chdir`](#chdir) | change the current working directory |
| 14 | `readdir` | retired: use SYS\_GETDENTS |
| 15 | [`unlink`](#unlink) | remove a file |
| 16 | [`rmdir`](#rmdir) | remove a directory |
| 17 | `ttyctl` | retired: ttyctl() is a libc wrapper over termios |
| 18 | [`pipe`](#pipe) | create a pipe (pair of connected file descriptors) |
| 19 | [`dup2`](#dup2) | make new\_fd another descriptor for old\_fd's open file |
| 20 | [`ftruncate`](#ftruncate) | cut or grow an open file to a length |
| 21 | [`lseek`](#lseek) | set the file offset |
| 22 | [`pollkey`](#pollkey) | non-blocking keyboard poll for interactive graphics programs |
| 23 | [`ioctl`](#ioctl) | issue a device-control request on an open file descriptor |
| 24 | [`mmap`](#mmap) |  |
| 25 | [`munmap`](#munmap) |  |
| 26 | [`mprotect`](#mprotect) |  |
| 27 | [`fork`](#fork) | create a child process that is a copy of the caller |
| 28 | [`execve`](#execve) | replace the calling process's program |
| 29 | [`getppid`](#getppid) | return the parent's pid, or 0 if the parent has exited |
| 30 | [`sched_yield`](#sched_yield) | let other ready processes run first |
| 31 | [`fcntl`](#fcntl) | control an open file descriptor |
| 32 | [`fstat`](#fstat) | get the metadata of an open file |
| 33 | [`rename`](#rename) | rename a file or directory |
| 34 | [`access`](#access) | check that a file exists and may be accessed |
| 35 | [`pread`](#pread) | read at an offset without moving the file offset |
| 36 | [`pwrite`](#pwrite) | write at an offset without moving the file offset |
| 37 | [`fsync`](#fsync) | write an open file's data to disk |
| 38 | [`getdents`](#getdents) | read directory entries |
| 39 | [`poll`](#poll) | wait until one of a set of descriptors is ready |
| 40 | [`kill`](#kill) | send a signal |
| 41 | [`sigaction`](#sigaction) | read and change what a process does with a signal |
| 42 | [`sigprocmask`](#sigprocmask) | read and change the blocked mask |
| 43 | [`sigreturn`](#sigreturn) | return from a signal handler (called by the restorer) |
| 44 | [`sigpending`](#sigpending) | the signals that are pending and blocked |
| 45 | [`sigsuspend`](#sigsuspend) | wait for a signal with a temporary mask |
| 46 | [`alarm`](#alarm) | send SIGALRM after a number of seconds (0 cancels) |
| 47 | [`setpgid`](#setpgid) | move a process into a process group |
| 48 | [`getpgid`](#getpgid) | the process group of a process (pid 0: the caller) |
| 49 | [`setsid`](#setsid) | start a new session, with the caller as the leader of it and of a new process group, and no controlling terminal |
| 50 | [`getsid`](#getsid) | the session of a process (pid 0: the caller) |
| 51 | [`clock_gettime`](#clock_gettime) | read a clock |
| 52 | [`nanosleep`](#nanosleep) | sleep for a time span |
| 53 | [`times`](#times) | the CPU time of the calling process and its reaped children |
| 54 | [`getuid`](#getuid) |  |
| 55 | [`geteuid`](#geteuid) |  |
| 56 | [`getgid`](#getgid) |  |
| 57 | [`getegid`](#getegid) |  |
| 58 | [`setuid`](#setuid) | args: EBX = uid |
| 59 | [`setgid`](#setgid) | args: EBX = gid |
| 60 | [`seteuid`](#seteuid) |  |
| 61 | [`setegid`](#setegid) |  |
| 62 | [`chmod`](#chmod) | args: path, mode (07777 bits) |
| 63 | [`fchmod`](#fchmod) | args: EBX = fd, ECX = mode |
| 64 | [`chown`](#chown) | args: path, uid, gid (-1 keeps one) |
| 65 | [`umask`](#umask) | args: EBX = mask |
| 66 | [`reboot`](#reboot) | restart (RB\_AUTOBOOT) or turn off (RB\_POWER\_OFF) the machine |
| 67 | [`shm_create`](#shm_create) | make a shared memory object (kernel/memory/shm.h) |
| 68 | [`socket`](#socket) | make an unconnected socket |
| 69 | [`bind`](#bind) | give a socket a name |
| 70 | [`listen`](#listen) | let a bound socket take connections |
| 71 | [`accept`](#accept) | take the next connection of a listening socket (accept4) |
| 72 | [`connect`](#connect) | connect to the listening socket with a name |
| 73 | [`socketpair`](#socketpair) | make two connected sockets |
| 74 | [`sendmsg`](#sendmsg) | send data, and pass open files with SCM\_RIGHTS |
| 75 | [`recvmsg`](#recvmsg) | receive data, and the files passed with it |
| 76 | [`uname`](#uname) | the name of the system, its release and version, and the machine |
| 77 | [`openat`](#openat) | open relative to a directory |
| 78 | [`mkdirat`](#mkdirat) | mkdir relative to a directory |
| 79 | [`unlinkat`](#unlinkat) | remove a file or (AT\_REMOVEDIR) a directory, relative to a directory |
| 80 | [`renameat`](#renameat) | rename, each path relative to its own directory |
| 81 | [`fstatat`](#fstatat) | stat a path relative to a directory |
| 82 | [`faccessat`](#faccessat) | access, relative to a directory |
| 83 | [`fchmodat`](#fchmodat) | args: dirfd, path, mode, flags (AT\_SYMLINK\_NOFOLLOW is refused: a link has no mode of its own, as on Linux) |
| 84 | [`fchownat`](#fchownat) | args: dirfd, path, uid, gid, flags (AT\_SYMLINK\_NOFOLLOW) |
| 85 | [`fchown`](#fchown) | args: fd, uid, gid |
| 86 | [`symlinkat`](#symlinkat) | make a symbolic link |
| 87 | [`readlinkat`](#readlinkat) | read the target of a symbolic link |
| 88 | [`linkat`](#linkat) | give a file one more name (a hard link) |
| 89 | [`utimensat`](#utimensat) | set a file's access and modification times |
| 90 | [`clock_settime`](#clock_settime) | set the realtime clock (root only) |
| 91 | [`sethostname`](#sethostname) | set the host name (root only) |
| 92 | [`getpriority`](#getpriority) | the nice value of processes |
| 93 | [`setpriority`](#setpriority) | set the nice value (-20 to 19) of processes; it sets the length of their time slice |
| 94 | [`flock`](#flock) | an advisory lock on a whole file (kernel/vfs/flock.h) |
| 95 | [`mknodat`](#mknodat) | make a named pipe, a device node or an empty file |
| 96 | [`chroot`](#chroot) | make a directory the caller's root: its paths start there, and ".." stops there (root only) |
| 97 | [`clone`](#clone) | make a thread of the current process |
| 98 | [`exit_group`](#exit_group) | end every thread of the current process |
| 99 | [`gettid`](#gettid) | the calling thread's id (the pid for a single thread) |
| 100 | [`tgkill`](#tgkill) | send a signal to one thread |
| 101 | [`set_tls`](#set_tls) | set the calling thread's thread pointer, which user code reaches its thread-local storage through (%fs:0 on x86\_64, %gs:0 on i686) |
| 102 | [`futex`](#futex) | args: uaddr (an aligned 32-bit word), op, val, timeout |
| 103 | [`getsockname`](#getsockname) | the local address of a socket |
| 104 | [`getpeername`](#getpeername) | the address a socket is connected to |
| 105 | [`setsockopt`](#setsockopt) | set a socket option (an int value) |
| 106 | [`getsockopt`](#getsockopt) | read a socket option (an int value) |
| 107 | [`shutdown`](#shutdown) | stop the reading or the writing on a socket |
| 108 | [`getrandom`](#getrandom) | random bytes |

## exit

Number 0. Handler: `sys_exit` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

terminate the current process.
arg: EBX = exit code.
never returns: the process becomes a zombie until its parent reaps it.

## write

Number 1. Handler: `sys_write` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

write bytes to a file descriptor.
args: EBX = fd, ECX = buffer pointer, EDX = count.
returns number of bytes written, or negative errno on error.

## read

Number 2. Handler: `sys_read` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

read bytes from a file descriptor.
args: EBX = fd, ECX = buffer pointer, EDX = count.
returns number of bytes read (0 at end of file), or negative errno.

## open

Number 3. Handler: `sys_open`.

## close

Number 4. Handler: `sys_close` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

close an open file descriptor.
args: EBX = fd.
returns 0 on success, negative errno on failure.

## stat

Number 5. Handler: `sys_stat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

get file metadata.
args: path, struct stat pointer (kernel/vfs/stat.h).
returns 0 on success, negative errno on failure.

## mkdir

Number 7. Handler: `sys_mkdir` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

create a directory.
args: path, mode.
returns 0 on success, negative errno on failure.

## exec

Number 8. Handler: `sys_exec` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

start an ELF binary as a child process (spawn, not execve).
args: EBX = path string pointer, ECX = argv array pointer (may be NULL).
returns the child's PID, or negative errno on failure.

## getpid

Number 9. Handler: `sys_getpid` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

return the current process ID (the thread group's id).

## waitpid

Number 10. Handler: `sys_waitpid` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

wait for a child process to exit and reap it.
args: EBX = pid (a child's pid, or -1 for any child),
      ECX = int \*status (may be NULL; receives the wait status,
            wait\_status.h), EDX = options (WNOHANG).
with WUNTRACED a stopped child and with WCONTINUED a continued one is
reported (once) too, without reaping it.
returns the child's pid; 0 with WNOHANG when no matching child has
anything to report yet; -ECHILD if there is no matching child; -EINVAL for a
pid of 0 or below -1 (process groups come later); -EFAULT for a bad
status pointer (the child is reaped anyway).

## sbrk

Number 11. Handler: `sys_sbrk`.

## getcwd

Number 12. Handler: `sys_getcwd` in [`src/kernel/syscall/sys_misc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_misc.c).

copy the current working directory into a user buffer.
args: EBX = buffer pointer, ECX = buffer size.
returns 0 on success, negative errno on failure.

## chdir

Number 13. Handler: `sys_chdir` in [`src/kernel/syscall/sys_misc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_misc.c).

change the current working directory.
args: EBX = path string pointer.
returns 0 on success, negative errno on failure.

## unlink

Number 15. Handler: `sys_unlink` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

remove a file.
args: path.
returns 0 on success, negative errno on failure.

## rmdir

Number 16. Handler: `sys_rmdir` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

remove a directory.
args: path.
returns 0 on success, negative errno on failure.

## pipe

Number 18. Handler: `sys_pipe` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

create a pipe (pair of connected file descriptors).
args: EBX = pointer to int\[2] array (receives \[read\_fd, write\_fd]).
returns 0 on success, negative errno on failure.

## dup2

Number 19. Handler: `sys_dup2` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

make new\_fd another descriptor for old\_fd's open file.
args: EBX = old\_fd, ECX = new\_fd.
if new\_fd is already open, it is closed first. the new descriptor does
not have FD\_CLOEXEC.
returns new\_fd on success, negative errno on failure.

## ftruncate

Number 20. Handler: `sys_ftruncate` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

cut or grow an open file to a length.
args: fd, length (the new bytes read as zeros). the offset stays.
returns 0, or a negative errno (EINVAL: a negative length, or a file
not open for writing; EFBIG: too long).

## lseek

Number 21. Handler: `sys_lseek` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

set the file offset.
args: EBX = fd, ECX = offset, EDX = whence (0=SET, 1=CUR, 2=END).
returns the new offset, or negative errno on error (ESPIPE for a pipe
or the console).

## pollkey

Number 22. Handler: `sys_pollkey` in [`src/kernel/syscall/sys_fb.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fb.c).

non-blocking keyboard poll for interactive graphics programs.
returns the next pending character-key (a "kbhit"-style peek+consume), or 0
if no key is waiting. special keys (arrows etc.) are consumed and reported
as 0 so they don't stall the caller.
takes no arguments.

## ioctl

Number 23. Handler: `sys_ioctl` in [`src/kernel/syscall/sys_misc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_misc.c).

issue a device-control request on an open file descriptor.
args: EBX = fd, ECX = request code, EDX = argument (usually a user pointer).
only file-backed fds carry a filesystem that can service ioctls; devfs
device nodes forward to the backing device. everything else is -ENOTTY.
returns 0 (or a request-specific value) on success, negative errno on error.

note: the arg pointer is request-specific, so its size is only known to the
device handler; this layer passes it through and the handler validates it.

## mmap

Number 24. Handler: `sys_mmap`.

## munmap

Number 25. Handler: `sys_munmap`.

## mprotect

Number 26. Handler: `sys_mprotect`.

## fork

Number 27. Handler: `sys_fork` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

create a child process that is a copy of the caller.
no arguments. returns the child's PID in the parent, 0 in the child (set
up by process\_fork), or -ENOMEM.

## execve

Number 28. Handler: `sys_execve` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

replace the calling process's program.
args: EBX = path, ECX = argv, EDX = envp (both NULL-terminated, may be
      NULL).
on success it does not return: the syscall frame now enters the new
program (exec\_replace). returns a negative errno on failure, with the
old program still running.

## getppid

Number 29. Handler: `sys_getppid` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

return the parent's pid, or 0 if the parent has exited.

## sched_yield

Number 30. Handler: `sys_sched_yield` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

let other ready processes run first. returns 0.

## fcntl

Number 31. Handler: `sys_fcntl` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

control an open file descriptor.
args: EBX = fd, ECX = command, EDX = argument.
  F\_DUPFD / F\_DUPFD\_CLOEXEC: duplicate to the lowest free fd &gt;= argument
  F\_GETFD / F\_SETFD: the descriptor flags (FD\_CLOEXEC)
  F\_GETFL / F\_SETFL: the description's status flags; only O\_APPEND and
                     O\_NONBLOCK can be changed
returns the command's result, or a negative errno.

## fstat

Number 32. Handler: `sys_fstat` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

get the metadata of an open file.
args: EBX = fd, ECX = struct stat pointer.
returns 0 on success, negative errno on failure.

## rename

Number 33. Handler: `sys_rename` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

rename a file or directory.
args: old path, new path (same filesystem).
returns 0 on success, negative errno on failure (EXDEV across mounts).

## access

Number 34. Handler: `sys_access` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

check that a file exists and may be accessed.
args: path, mode (F\_OK, or R\_OK | W\_OK | X\_OK).
returns 0, or a negative errno (ENOENT, EACCES).

## pread

Number 35. Handler: `sys_pread` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

read at an offset without moving the file offset.
args: EBX = fd, ECX = buffer, EDX = count, ESI = offset.
returns bytes read, or negative errno (ESPIPE for a pipe or the console).

## pwrite

Number 36. Handler: `sys_pwrite` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

write at an offset without moving the file offset.
args: EBX = fd, ECX = buffer, EDX = count, ESI = offset.
returns bytes written, or negative errno (EINVAL with O\_APPEND).

## fsync

Number 37. Handler: `sys_fsync` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

write an open file's data to disk.
args: EBX = fd.
ext2 writes through to the disk at once (there is no cache), so this
only checks the descriptor. returns 0 or -EBADF.

## getdents

Number 38. Handler: `sys_getdents` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

read directory entries.
args: EBX = fd of an open directory, ECX = buffer, EDX = buffer size.
fills the buffer with as many whole records (kernel/vfs/dirent.h) as fit.
returns the bytes written, 0 at the end of the directory, or a negative
errno (EINVAL if the buffer cannot hold the next record, ENOTDIR if fd
is not a directory).

## poll

Number 39. Handler: `sys_poll` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

wait until one of a set of descriptors is ready.
args: EBX = struct pollfd array, ECX = count (at most 64),
      EDX = timeout in milliseconds (negative: no limit, 0: do not wait).
returns the number of descriptors with events, 0 on timeout, or a
negative errno.

## kill

Number 40. Handler: `sys_kill` in [`src/kernel/signal/signal.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/signal/signal.c).

send a signal.
args: EBX = pid: a process (&gt; 0), the caller's process group (0), every
            process but init (-1), or the process group -pid (&lt; -1);
      ECX = signal (0 only checks that the target exists).
returns 0, or -ESRCH, -EINVAL.

## sigaction

Number 41. Handler: `sys_sigaction` in [`src/kernel/signal/signal.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/signal/signal.c).

read and change what a process does with a signal.
args: EBX = signal, ECX = new action (ksigaction\_t, may be NULL),
      EDX = old action out (may be NULL).
SIGKILL and SIGSTOP cannot be changed. returns 0 or a negative errno.

## sigprocmask

Number 42. Handler: `sys_sigprocmask` in [`src/kernel/signal/signal.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/signal/signal.c).

read and change the blocked mask.
args: EBX = how (SIG\_BLOCK, SIG\_UNBLOCK, SIG\_SETMASK), ECX = set
      (uint32\_t \*, may be NULL), EDX = old set out (may be NULL).
returns 0 or a negative errno.

## sigreturn

Number 43. Handler: `sys_sigreturn` in [`src/kernel/signal/signal.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/signal/signal.c).

return from a signal handler (called by the restorer).
restores the registers and mask saved in the handler's frame; the
result is the interrupted code's EAX.

## sigpending

Number 44. Handler: `sys_sigpending` in [`src/kernel/signal/signal.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/signal/signal.c).

the signals that are pending and blocked.
args: EBX = uint32\_t \* out.

## sigsuspend

Number 45. Handler: `sys_sigsuspend` in [`src/kernel/signal/signal.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/signal/signal.c).

wait for a signal with a temporary mask.
args: EBX = uint32\_t \* mask.
the mask is in place until a signal comes; after its handler (or its
default action) the old mask is back. always returns -EINTR.

## alarm

Number 46. Handler: `sys_alarm` in [`src/kernel/signal/signal.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/signal/signal.c).

send SIGALRM after a number of seconds (0 cancels).
returns the seconds that were left of the previous alarm (rounded up).

## setpgid

Number 47. Handler: `sys_setpgid` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

move a process into a process group.
args: EBX = pid (0: the caller; else the caller or one of its children),
      ECX = pgid (0: the target's own pid, a new group).
the group must be new or exist in the caller's session.
returns 0, or -ESRCH, -EPERM, -EINVAL.

## getpgid

Number 48. Handler: `sys_getpgid` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

the process group of a process (pid 0: the caller).

## setsid

Number 49. Handler: `sys_setsid` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

start a new session, with the caller as the leader of it and
of a new process group, and no controlling terminal.
returns the new session id, or -EPERM if the caller leads a group.

## getsid

Number 50. Handler: `sys_getsid` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

the session of a process (pid 0: the caller).

## clock_gettime

Number 51. Handler: `sys_clock_gettime` in [`src/kernel/time/clock.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/time/clock.c).

read a clock.
args: EBX = clock id (CLOCK\_\*), ECX = struct timespec \* out.
returns 0, or -EINVAL for an unknown clock, -EFAULT.

## nanosleep

Number 52. Handler: `sys_nanosleep` in [`src/kernel/time/clock.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/time/clock.c).

sleep for a time span.
args: EBX = const struct timespec \* span, ECX = struct timespec \* left
      (may be NULL).
the sleep ends at the first timer tick after the span (10 ms steps). a
signal ends it early: -EINTR, with the time left in \*left.
returns 0, or -EINTR, -EINVAL (a negative span or nanoseconds out of
range), -EFAULT.

## times

Number 53. Handler: `sys_times` in [`src/kernel/time/clock.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/time/clock.c).

the CPU time of the calling process and its reaped children.
args: EBX = struct tms \* out (may be NULL).
returns the ticks since boot (clock\_t, TIMER\_HZ per second).

## getuid

Number 54. Handler: `sys_getuid`.

## geteuid

Number 55. Handler: `sys_geteuid`.

## getgid

Number 56. Handler: `sys_getgid`.

## getegid

Number 57. Handler: `sys_getegid`.

## setuid

Number 58. Handler: `sys_setuid` in [`src/kernel/syscall/sys_cred.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_cred.c).

args: EBX = uid. returns 0 or -EPERM.

## setgid

Number 59. Handler: `sys_setgid` in [`src/kernel/syscall/sys_cred.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_cred.c).

args: EBX = gid. returns 0 or -EPERM. root's check is on the
effective user id, so set the group before giving up root.

## seteuid

Number 60. Handler: `sys_seteuid`.

## setegid

Number 61. Handler: `sys_setegid`.

## chmod

Number 62. Handler: `sys_chmod` in [`src/kernel/syscall/sys_cred.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_cred.c).

args: path, mode (07777 bits).
only the owner or root. returns 0 or a negative errno.

## fchmod

Number 63. Handler: `sys_fchmod` in [`src/kernel/syscall/sys_cred.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_cred.c).

args: EBX = fd, ECX = mode. like chmod, on an open file.

## chown

Number 64. Handler: `sys_chown` in [`src/kernel/syscall/sys_cred.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_cred.c).

args: path, uid, gid (-1 keeps one).
returns 0 or a negative errno (see owner\_change).

## umask

Number 65. Handler: `sys_umask` in [`src/kernel/syscall/sys_cred.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_cred.c).

args: EBX = mask. returns the old mask.

## reboot

Number 66. Handler: `sys_reboot` in [`src/kernel/syscall/sys_misc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_misc.c).

restart (RB\_AUTOBOOT) or turn off (RB\_POWER\_OFF) the
machine. only root (effective user 0) may. the disk writes are already
done: the block cache writes through.
returns -EPERM for other users and -EINVAL for another value; it does
not return for root.

## shm_create

Number 67. Handler: `sys_shm_create` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

make a shared memory object (kernel/memory/shm.h).
args: EBX = size in bytes (rounded up to pages), ECX = fd flags
      (FD\_CLOEXEC).
returns the new fd (open read-write; map it with mmap), or -EINVAL,
-ENOMEM, -EMFILE.

## socket

Number 68. Handler: `sys_socket` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

make an unconnected socket.
args: domain (AF\_UNIX or AF\_INET), type (SOCK\_STREAM; for AF\_INET also
      SOCK\_DGRAM; with SOCK\_NONBLOCK and SOCK\_CLOEXEC), protocol (0; for
      AF\_INET also IPPROTO\_TCP, IPPROTO\_UDP or IPPROTO\_ICMP).
returns the new fd, or -EAFNOSUPPORT, -EPROTONOSUPPORT, -ENOMEM, -EMFILE.

## bind

Number 69. Handler: `sys_bind` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

give a socket a name.
args: fd, const struct sockaddr\_un \*, addrlen.
returns 0, or -EBADF, -ENOTSOCK, -EINVAL, -EADDRINUSE, -EFAULT.

## listen

Number 70. Handler: `sys_listen` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

let a bound socket take connections.
args: fd, backlog (AF\_UNIX: 0 or more than 16 means 16; AF\_INET: at most
      TCP\_BACKLOG\_MAX).
returns 0, or -EBADF, -ENOTSOCK, -EINVAL, -EOPNOTSUPP (a datagram socket).

## accept

Number 71. Handler: `sys_accept` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

take the next connection of a listening socket (accept4).
args: fd, struct sockaddr \* (or NULL), socklen\_t \*addrlen (or NULL),
      flags (SOCK\_NONBLOCK, SOCK\_CLOEXEC).
AF\_INET: the peer's sockaddr\_in. AF\_UNIX: the peer has no name, so only
sun\_family is filled in and \*addrlen becomes 2.
returns the new fd, or -EAGAIN, -EINVAL, -EBADF, -ENOTSOCK, -EFAULT,
-EMFILE, -ENOMEM.

## connect

Number 72. Handler: `sys_connect` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

connect to the listening socket with a name.
args: fd, const struct sockaddr\_un \*, addrlen.
returns 0, or -ECONNREFUSED (no one listens there, or the backlog is
full), -EISCONN, -EBADF, -ENOTSOCK, -EINVAL, -EFAULT.

## socketpair

Number 73. Handler: `sys_socketpair` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

make two connected sockets.
args: domain, type, protocol (as for socket), int sv\[2].
returns 0, or a negative errno.

## sendmsg

Number 74. Handler: `sys_sendmsg` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

send data, and pass open files with SCM\_RIGHTS.
args: fd, const struct msghdr \*, flags (MSG\_DONTWAIT, MSG\_NOSIGNAL).
the files go with the first byte sent, so a message that passes files
must have some data. msg\_name is ignored (the socket is connected).
returns the bytes sent, or -EPIPE (and SIGPIPE, unless MSG\_NOSIGNAL),
-EAGAIN, -ENOTCONN, -EBADF, -ENOTSOCK, -EINVAL, -EFAULT, -ENOMEM.

## recvmsg

Number 75. Handler: `sys_recvmsg` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

receive data, and the files passed with it.
args: fd, struct msghdr \*, flags (MSG\_DONTWAIT, MSG\_CMSG\_CLOEXEC).
the files come as one SCM\_RIGHTS control message. files that do not fit
in msg\_control (or in the fd table) are closed, and msg\_flags gets
MSG\_CTRUNC. msg\_namelen becomes 0 (the peer has no name).
returns the bytes received (0 at the end of the stream), or -EAGAIN,
-ENOTCONN, -EBADF, -ENOTSOCK, -EINVAL, -EFAULT.

## uname

Number 76. Handler: `sys_uname` in [`src/kernel/syscall/sys_uname.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_uname.c).

the name of the system, its release and version, and the
machine.
args: the user's struct utsname.
returns 0, or a negative errno.

## openat

Number 77. Handler: `sys_openat` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

open relative to a directory.
args: dirfd (AT\_FDCWD: the working directory), path, flags, mode.

## mkdirat

Number 78. Handler: `sys_mkdirat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

mkdir relative to a directory.
args: dirfd, path, mode.

## unlinkat

Number 79. Handler: `sys_unlinkat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

remove a file or (AT\_REMOVEDIR) a directory, relative to a
directory.
args: dirfd, path, flags.

## renameat

Number 80. Handler: `sys_renameat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

rename, each path relative to its own directory.
args: old dirfd, old path, new dirfd, new path.

## fstatat

Number 81. Handler: `sys_fstatat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

stat a path relative to a directory.
args: dirfd, path, struct stat pointer, flags (AT\_SYMLINK\_NOFOLLOW).

## faccessat

Number 82. Handler: `sys_faccessat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

access, relative to a directory.
args: dirfd, path, mode, flags (AT\_EACCESS).

## fchmodat

Number 83. Handler: `sys_fchmodat` in [`src/kernel/syscall/sys_cred.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_cred.c).

args: dirfd, path, mode, flags (AT\_SYMLINK\_NOFOLLOW is
refused: a link has no mode of its own, as on Linux).

## fchownat

Number 84. Handler: `sys_fchownat` in [`src/kernel/syscall/sys_cred.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_cred.c).

args: dirfd, path, uid, gid, flags (AT\_SYMLINK\_NOFOLLOW).

## fchown

Number 85. Handler: `sys_fchown` in [`src/kernel/syscall/sys_cred.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_cred.c).

args: fd, uid, gid. like chown, on an open file.

## symlinkat

Number 86. Handler: `sys_symlinkat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

make a symbolic link.
args: target (the path the link points at; it need not exist), dirfd,
      path of the new link.
the link belongs to the caller's effective user and group.
returns 0, or a negative errno (EEXIST, ENOENT, EACCES, ENAMETOOLONG).

## readlinkat

Number 87. Handler: `sys_readlinkat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

read the target of a symbolic link.
args: dirfd, path, buffer, buffer size.
returns the bytes put in the buffer (no NUL; a longer target is cut),
or a negative errno (EINVAL: not a link).

## linkat

Number 88. Handler: `sys_linkat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

give a file one more name (a hard link).
args: old dirfd, old path, new dirfd, new path, flags (AT\_SYMLINK\_FOLLOW:
      link to what a symbolic link points at, not the link itself).
returns 0, or a negative errno (EXDEV across mounts, EPERM for a
directory, EEXIST, EMLINK).

## utimensat

Number 89. Handler: `sys_utimensat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

set a file's access and modification times.
args: dirfd, path (NULL: the file open as dirfd, which is futimens),
      times (two timespecs, access then modification; NULL: both now),
      flags (AT\_SYMLINK\_NOFOLLOW).
a tv\_nsec of UTIME\_NOW takes the time now; UTIME\_OMIT keeps the time.
returns 0, or a negative errno (EPERM, EACCES, EINVAL, ENOENT, EBADF).

## clock_settime

Number 90. Handler: `sys_clock_settime` in [`src/kernel/time/clock.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/time/clock.c).

set the realtime clock (root only).
args: clock id (CLOCK\_REALTIME only), struct timespec \*.
the clock keeps whole seconds; the battery-backed clock (RTC) is not
written, so the next boot reads its old time again.
returns 0, or -EPERM, -EINVAL (another clock, a bad time), -EFAULT.

## sethostname

Number 91. Handler: `sys_sethostname` in [`src/kernel/syscall/sys_uname.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_uname.c).

set the host name (root only).
args: the name, its length (at most 64 bytes, no NUL needed).
returns 0, or -EPERM, -EINVAL (too long), -EFAULT.

## getpriority

Number 92. Handler: `sys_getpriority` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

the nice value of processes.
args: which (PRIO\_PROCESS, PRIO\_PGRP, PRIO\_USER), who (0: the caller's).
returns 20 - nice of the one with the most CPU (1 to 40, never
negative), or a negative errno (ESRCH: none, EINVAL).

## setpriority

Number 93. Handler: `sys_setpriority` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

set the nice value (-20 to 19) of processes; it sets
the length of their time slice. only root may lower it.
args: which, who (as getpriority), the nice value.
returns 0, or a negative errno (ESRCH, EPERM, EACCES, EINVAL).

## flock

Number 94. Handler: `sys_flock` in [`src/kernel/syscall/sys_io.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_io.c).

an advisory lock on a whole file (kernel/vfs/flock.h).
args: fd, operation (LOCK\_SH or LOCK\_EX, with LOCK\_NB; or LOCK\_UN).
returns 0, or a negative errno (EWOULDBLOCK, EINTR, EBADF, EINVAL).

## mknodat

Number 95. Handler: `sys_mknodat` in [`src/kernel/syscall/sys_fs.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_fs.c).

make a named pipe, a device node or an empty file.
args: dirfd, path, mode (S\_IFIFO, S\_IFCHR, S\_IFBLK or S\_IFREG, with the
      permission bits, taken through the umask), the device number.
a device node needs root. it opens with ENXIO: the devices are in /dev.
returns 0, or a negative errno (EEXIST, EPERM, EINVAL, EACCES).

## chroot

Number 96. Handler: `sys_chroot` in [`src/kernel/syscall/sys_misc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_misc.c).

make a directory the caller's root: its paths start there,
and ".." stops there (root only). the working directory stays; a tool
then changes it to "/".
args: path.
returns 0, or a negative errno (EPERM, ENOTDIR, ENOENT, EACCES).

## clone

Number 97. Handler: `sys_clone` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

make a thread of the current process.
args: flags, the new thread's user stack pointer, parent\_tid,
      child\_tid, tls.
only a thread can be made: flags must have CLONE\_VM, FS, FILES, SIGHAND
and THREAD (fork makes a process). SETTLS gives the thread tls as its
thread pointer (else it has the caller's). PARENT\_SETTID and CHILD\_SETTID write
the new thread's id to parent\_tid and child\_tid; CHILD\_CLEARTID makes
the kernel write 0 there when the thread ends. SYSVSEM is accepted and
has no effect.
returns the new thread's id in the caller and 0 in the thread; -EINVAL
for other flags or no stack, -EFAULT for a bad tid address, -EAGAIN if
the process table or the memory ran out.

## exit_group

Number 98. Handler: `sys_exit_group` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

end every thread of the current process.
arg: the exit code. never returns.

## gettid

Number 99. Handler: `sys_gettid` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

the calling thread's id (the pid for a single thread).

## tgkill

Number 100. Handler: `sys_tgkill` in [`src/kernel/signal/signal.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/signal/signal.c).

send a signal to one thread.
args: the thread group (pid), the thread's id, the signal (0 only checks
      that the thread exists).
SIGCONT continues the whole process, as for kill.
returns 0, or -ESRCH, -EINVAL.

## set_tls

Number 101. Handler: `sys_set_tls` in [`src/kernel/syscall/sys_proc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_proc.c).

set the calling thread's thread pointer, which user code
reaches its thread-local storage through (%fs:0 on x86\_64, %gs:0 on
i686). arg: the address. returns 0.

## futex

Number 102. Handler: `sys_futex` in [`src/kernel/sync/futex.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/sync/futex.c).

args: uaddr (an aligned 32-bit word), op, val, timeout.
FUTEX\_WAIT sleeps while \*uaddr == val, for at most \*timeout (a relative
struct timespec; NULL: no limit). FUTEX\_WAKE wakes at most val threads
and returns how many it woke. FUTEX\_PRIVATE\_FLAG may be set: every
futex is private to its process here.
returns as above, -EINVAL (a bad address or time), -ENOSYS (another op).

## getsockname

Number 103. Handler: `sys_getsockname` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

the local address of a socket.
args: fd, struct sockaddr \*, socklen\_t \*addrlen.
returns 0, or -EBADF, -ENOTSOCK, -EFAULT.

## getpeername

Number 104. Handler: `sys_getpeername` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

the address a socket is connected to.
args: fd, struct sockaddr \*, socklen\_t \*addrlen.
returns 0, or -ENOTCONN, -EBADF, -ENOTSOCK, -EFAULT.

## setsockopt

Number 105. Handler: `sys_setsockopt` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

set a socket option (an int value).
args: fd, level, name, const void \*value, socklen\_t length.
a Unix socket takes the SOL\_SOCKET options and keeps none of them.
returns 0, or -ENOPROTOOPT, -EINVAL, -EBADF, -ENOTSOCK, -EFAULT.

## getsockopt

Number 106. Handler: `sys_getsockopt` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

read a socket option (an int value).
args: fd, level, name, void \*value, socklen\_t \*length.
returns 0, or -ENOPROTOOPT, -EINVAL, -EBADF, -ENOTSOCK, -EFAULT.

## shutdown

Number 107. Handler: `sys_shutdown` in [`src/kernel/syscall/sys_socket.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_socket.c).

stop the reading or the writing on a socket.
args: fd, how (SHUT\_RD, SHUT\_WR, SHUT\_RDWR).
returns 0, or -EINVAL, -EOPNOTSUPP (a Unix socket), -EBADF, -ENOTSOCK.

## getrandom

Number 108. Handler: `sys_getrandom` in [`src/kernel/syscall/sys_misc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/syscall/sys_misc.c).

random bytes.
args: buf, length, flags (GRND\_NONBLOCK, GRND\_RANDOM, GRND\_INSECURE).
waits until the generator is seeded, unless GRND\_NONBLOCK or
GRND\_INSECURE.
returns the bytes given, or -EAGAIN, -EINVAL, -EFAULT, -EINTR.

#pragma once

/**
 * @file syscall.h
 * @brief This file contains the declarations of system call numbers and the raw syscall interface.
 */

/* ginnOS syscall numbers */
#define SYS_EXIT 0     // exit the current process
#define SYS_WRITE 1    // write to a file descriptor
#define SYS_READ 2     // read from a file descriptor
#define SYS_OPEN 3     // open a file
#define SYS_CLOSE 4    // close a file descriptor
#define SYS_STAT 5     // get file status
#define SYS_CREATE 6   // retired: create() is open(O_CREAT | O_WRONLY | O_TRUNC)
#define SYS_MKDIR 7    // create a new directory
#define SYS_EXEC 8     // execute a program
#define SYS_GETPID 9   // get the current process ID
#define SYS_WAITPID 10 // wait for a child process to change state
#define SYS_SBRK 11    // increase program data space
#define SYS_GETCWD 12  // get current working directory
#define SYS_CHDIR 13   // change current working directory
#define SYS_READDIR 14 // retired: use SYS_GETDENTS
#define SYS_UNLINK 15  // remove a file or directory
#define SYS_RMDIR 16   // remove a directory
#define SYS_TTYCTL 17  // retired: ttyctl() is a libc wrapper over termios
#define SYS_PIPE 18    // create a pipe
#define SYS_DUP2 19
#define SYS_FTRUNCATE 20
#define SYS_LSEEK 21    // duplicate a file descriptor
#define SYS_POLLKEY 22  // non-blocking: return a pending char key, or 0
#define SYS_IOCTL 23    // device-control request on a file descriptor
#define SYS_MMAP 24     // map memory (anonymous or device-backed)
#define SYS_MUNMAP 25   // unmap a range of memory
#define SYS_MPROTECT 26 // change the protection of a range of memory
#define SYS_FORK 27     // create a copy of the calling process
#define SYS_EXECVE 28   // replace the calling process's program
#define SYS_GETPPID 29  // get the parent's process ID
#define SYS_SCHED_YIELD 30 // let other processes run first
#define SYS_FCNTL 31    // control an open file descriptor
#define SYS_FSTAT 32    // metadata of an open file
#define SYS_RENAME 33   // rename a file
#define SYS_ACCESS 34   // check that a file can be accessed
#define SYS_PREAD 35    // read at an offset
#define SYS_PWRITE 36   // write at an offset
#define SYS_FSYNC 37    // write a file's data to disk
#define SYS_GETDENTS 38 // read directory records
#define SYS_POLL 39     // wait for file descriptors to become ready
#define SYS_KILL 40     // send a signal
#define SYS_SIGACTION 41 // read and change a signal's action
#define SYS_SIGPROCMASK 42 // read and change the blocked signals
#define SYS_SIGRETURN 43 // return from a signal handler
#define SYS_SIGPENDING 44 // blocked signals that are pending
#define SYS_SIGSUSPEND 45 // wait for a signal
#define SYS_ALARM 46    // SIGALRM after a number of seconds
#define SYS_SETPGID 47  // move a process into a process group
#define SYS_GETPGID 48  // the process group of a process
#define SYS_SETSID 49   // start a new session
#define SYS_GETSID 50   // the session of a process
#define SYS_CLOCK_GETTIME 51 // read a clock
#define SYS_NANOSLEEP 52 // sleep for a time span
#define SYS_TIMES 53    // process CPU times
#define SYS_GETUID 54 // real user id
#define SYS_GETEUID 55 // effective user id
#define SYS_GETGID 56 // real group id
#define SYS_GETEGID 57 // effective group id
#define SYS_SETUID 58 // set the user ids
#define SYS_SETGID 59 // set the group ids
#define SYS_SETEUID 60 // set the effective user id
#define SYS_SETEGID 61 // set the effective group id
#define SYS_CHMOD 62 // change a file's mode
#define SYS_FCHMOD 63 // change an open file's mode
#define SYS_CHOWN 64 // change a file's owner
#define SYS_UMASK 65 // set the file creation mask
#define SYS_REBOOT 66 // restart the machine (root only)
#define SYS_SHM_CREATE 67 // make a shared memory object, return its fd
#define SYS_SOCKET 68     // make a Unix domain socket
#define SYS_BIND 69       // give a socket a name
#define SYS_LISTEN 70     // let a bound socket take connections
#define SYS_ACCEPT 71     // take a connection (accept4 flags)
#define SYS_CONNECT 72    // connect to a named socket
#define SYS_SOCKETPAIR 73 // make two connected sockets
#define SYS_SENDMSG 74    // send data and files
#define SYS_RECVMSG 75    // receive data and files
#define SYS_UNAME 76      // the name, release and machine of the system
#define SYS_OPENAT 77     // the *at calls: a path relative to a directory fd
#define SYS_MKDIRAT 78
#define SYS_UNLINKAT 79
#define SYS_RENAMEAT 80
#define SYS_FSTATAT 81
#define SYS_FACCESSAT 82
#define SYS_FCHMODAT 83
#define SYS_FCHOWNAT 84
#define SYS_FCHOWN 85
#define SYS_SYMLINKAT 86  // links: a symbolic link, its target, a hard link
#define SYS_READLINKAT 87
#define SYS_LINKAT 88
#define SYS_UTIMENSAT 89  // set a file's times
#define SYS_CLOCK_SETTIME 90 // set the realtime clock (root)
#define SYS_SETHOSTNAME 91   // set the host name (root)
#define SYS_GETPRIORITY 92   // the nice value of processes
#define SYS_SETPRIORITY 93
#define SYS_FLOCK 94         // an advisory lock on a whole file

/**
 * the raw syscall (src/libc/arch/<arch>/syscall.asm). the arguments and
 * the result are pointer-wide, so a pointer passes through unchanged. a
 * result from -4095 to -1 is a negative errno.
 */
long _syscall(long num, long arg1, long arg2, long arg3, long arg4, long arg5);

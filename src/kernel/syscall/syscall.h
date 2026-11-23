#pragma once

#include "common/stdint.h"

// syscall vector number (int 0x80)
#define SYSCALL_VECTOR 0x80

// syscall numbers (EAX)
#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_OPEN 3
#define SYS_CLOSE 4
#define SYS_STAT 5
#define SYS_CREATE 6 // retired: create() is open(O_CREAT | O_WRONLY | O_TRUNC)
#define SYS_MKDIR 7
#define SYS_EXEC 8
#define SYS_GETPID 9
#define SYS_WAITPID 10
#define SYS_SBRK 11
#define SYS_GETCWD 12
#define SYS_CHDIR 13
#define SYS_READDIR 14 // retired: use SYS_GETDENTS
#define SYS_UNLINK 15
#define SYS_RMDIR 16
#define SYS_TTYCTL 17 // retired: ttyctl() is a libc wrapper over termios
#define SYS_PIPE 18
#define SYS_DUP2 19
#define SYS_FTRUNCATE 20
#define SYS_LSEEK 21
#define SYS_POLLKEY 22
#define SYS_IOCTL 23
#define SYS_MMAP 24
#define SYS_MUNMAP 25
#define SYS_MPROTECT 26
#define SYS_FORK 27
#define SYS_EXECVE 28
#define SYS_GETPPID 29
#define SYS_SCHED_YIELD 30
#define SYS_FCNTL 31
#define SYS_FSTAT 32
#define SYS_RENAME 33
#define SYS_ACCESS 34
#define SYS_PREAD 35
#define SYS_PWRITE 36
#define SYS_FSYNC 37
#define SYS_GETDENTS 38
#define SYS_POLL 39
#define SYS_KILL 40
#define SYS_SIGACTION 41
#define SYS_SIGPROCMASK 42
#define SYS_SIGRETURN 43
#define SYS_SIGPENDING 44
#define SYS_SIGSUSPEND 45
#define SYS_ALARM 46
#define SYS_SETPGID 47
#define SYS_GETPGID 48
#define SYS_SETSID 49
#define SYS_GETSID 50
#define SYS_CLOCK_GETTIME 51
#define SYS_NANOSLEEP 52
#define SYS_TIMES 53
#define SYS_GETUID 54
#define SYS_GETEUID 55
#define SYS_GETGID 56
#define SYS_GETEGID 57
#define SYS_SETUID 58
#define SYS_SETGID 59
#define SYS_SETEUID 60
#define SYS_SETEGID 61
#define SYS_CHMOD 62
#define SYS_FCHMOD 63
#define SYS_CHOWN 64
#define SYS_UMASK 65
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

// total number of syscalls defined
#define SYSCALL_COUNT 92

/**
 * initialize the system call interface.
 * registers the int 0x80 handler and opens the gate to ring 3.
 * must be called after isr_initialize().
 */
void syscall_initialize(void);

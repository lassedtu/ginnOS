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

// total number of syscalls defined
#define SYSCALL_COUNT 51

/**
 * initialize the system call interface.
 * registers the int 0x80 handler and opens the gate to ring 3.
 * must be called after isr_initialize().
 */
void syscall_initialize(void);

#pragma once

#include "../../common/stdint.h"

// syscall vector number (int 0x80)
#define SYSCALL_VECTOR 0x80

// syscall numbers (EAX)
#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_OPEN 3
#define SYS_CLOSE 4
#define SYS_STAT 5
#define SYS_CREATE 6
#define SYS_MKDIR 7
#define SYS_EXEC 8
#define SYS_GETPID 9
#define SYS_WAITPID 10
#define SYS_SBRK 11
#define SYS_GETCWD 12
#define SYS_CHDIR 13
#define SYS_READDIR 14
#define SYS_UNLINK 15
#define SYS_RMDIR 16
#define SYS_TTYCTL 17
#define SYS_PIPE 18
#define SYS_DUP2 19
#define SYS_FTRUNCATE 20
#define SYS_LSEEK 21

// total number of syscalls defined
#define SYSCALL_COUNT 22

/**
 * initialize the system call interface.
 * registers the int 0x80 handler and opens the gate to ring 3.
 * must be called after isr_initialize().
 */
void syscall_initialize(void);

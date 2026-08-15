#pragma once

/**
 * @file syscall.h
 * @brief This file contains the declarations of system call numbers and the raw syscall interface.
 */

/* ginnOS syscall numbers */
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

/* raw syscall interface  implemented in syscall.asm */
int _syscall(int num, int arg1, int arg2, int arg3, int arg4, int arg5);

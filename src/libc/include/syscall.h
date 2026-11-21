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
#define SYS_TTYCTL 17  // control terminal behavior
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

/* raw syscall interface  implemented in syscall.asm */
int _syscall(int num, int arg1, int arg2, int arg3, int arg4, int arg5);

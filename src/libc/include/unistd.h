#pragma once

/**
 * @file unistd.h
 * @brief This file contains the declarations of POSIX-like system call wrappers.
 */

#include "syscall.h"

typedef unsigned int size_t;
typedef int ssize_t;
typedef int pid_t;

/**
 * write bytes to a file descriptor.
 * @param fd file descriptor (1 = stdout, 2 = stderr).
 * @param buf pointer to data to write.
 * @param count number of bytes to write.
 * @return number of bytes written, or -1 on error.
 */
ssize_t write(int fd, const void *buf, size_t count);

/**
 * read bytes from a file descriptor.
 * @param fd file descriptor (0 = stdin).
 * @param buf buffer to read into.
 * @param count maximum bytes to read.
 * @return number of bytes read, or -1 on error.
 */
ssize_t read(int fd, void *buf, size_t count);

/**
 * open a file by path.
 * @param path file path.
 * @param flags open flags (unused for now).
 * @return file descriptor on success, -1 on failure.
 */
int open(const char *path, int flags);

/**
 * close a file descriptor.
 * @param fd file descriptor to close.
 * @return 0 on success, -1 on failure.
 */
int close(int fd);

/**
 * execute a program, spawning a child process.
 * @param path path to the ELF executable.
 * @return child PID on success, -1 on failure.
 */
pid_t exec(const char *path);

/**
 * wait for a child process to exit.
 * @param pid child PID to wait for.
 * @return child's exit code, or -1 on error.
 */
int waitpid(pid_t pid);

/**
 * terminate the current process.
 * @param status exit code.
 */
void _exit(int status) __attribute__((noreturn));

/**
 * get the current process ID.
 * @return the current PID.
 */
pid_t getpid(void);

/**
 * grow the program break (allocate heap memory).
 * @param increment number of bytes to grow by.
 * @return pointer to the previous break, or (void*)-1 on failure.
 */
void *sbrk(int increment);

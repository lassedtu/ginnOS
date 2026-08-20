#pragma once

/**
 * posix-compatible errno constants for ginnOS userspace.
 *
 * syscalls return negative errno values on failure. the libc wrappers
 * extract the error, store it in the global errno variable, and return -1
 * (or NULL, etc.) to the caller.
 */

#define ENOSYS  1  /* function not implemented */
#define EBADF   2  /* bad file descriptor */
#define EINVAL  3  /* invalid argument */
#define ENOMEM  4  /* not enough memory */
#define ENOENT  5  /* no such file or directory */
#define EACCES  6  /* permission denied */
#define EPIPE   7  /* broken pipe */
#define ESPIPE  8  /* invalid seek */
#define EEXIST  9  /* file exists */
#define EISDIR  10 /* is a directory */
#define ENOTDIR 11 /* not a directory */
#define EMFILE  12 /* too many open files */
#define ENFILE  13 /* too many open files in system */
#define EIO     14 /* i/o error */
#define ENOSPC  15 /* no space left on device */
#define EBUSY   16 /* resource busy */
#define ERANGE  17 /* result too large */
#define EFAULT  18 /* bad address */
#define ESRCH   19 /* no such process */
#define ECHILD  20 /* no child processes */
#define ENOBUFS 21 /* no buffer space available */

/* global errno variable, set by libc wrappers on syscall failure */
extern int errno;

/**
 * print a message followed by the string for the current errno.
 * @param s prefix string (may be NULL).
 */
void perror(const char *s);

/**
 * return a string describing the given error number.
 * @param errnum error number (one of the E* constants above).
 * @return pointer to a static string.
 */
const char *strerror(int errnum);

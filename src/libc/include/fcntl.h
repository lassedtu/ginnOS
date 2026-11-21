#pragma once

/**
 * @file fcntl.h
 * @brief file open flags and open().
 *
 * the O_ values are ABI and must match the kernel side in
 * src/kernel/vfs/file.h exactly.
 */

typedef unsigned int mode_t;

// access modes (exactly one)
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_ACCMODE 0x0003

// creation and status flags (any combination)
#define O_CREAT     0x0040  // create the file if it does not exist
#define O_EXCL      0x0080  // with O_CREAT: fail if it exists
#define O_TRUNC     0x0200  // truncate a regular file opened for writing
#define O_APPEND    0x0400  // every write goes to the end of the file
#define O_NONBLOCK  0x0800  // return EAGAIN instead of waiting (pipes)
#define O_DIRECTORY 0x10000 // fail with ENOTDIR unless it is a directory
#define O_CLOEXEC   0x80000 // close the descriptor on execve

// fcntl commands
#define F_DUPFD 0          // duplicate to the lowest free fd >= arg
#define F_GETFD 1          // get the descriptor flags
#define F_SETFD 2          // set the descriptor flags
#define F_GETFL 3          // get the status flags and access mode
#define F_SETFL 4          // set O_APPEND and O_NONBLOCK
#define F_DUPFD_CLOEXEC 1030 // F_DUPFD with FD_CLOEXEC set

// descriptor flags
#define FD_CLOEXEC 1

/**
 * control an open file descriptor.
 * @param fd the descriptor.
 * @param cmd an F_ command.
 * @param ... the command's int argument, if it takes one.
 * @return the command's result, or -1 (errno set).
 */
int fcntl(int fd, int cmd, ...);

/**
 * open a file.
 * @param path the file's path.
 * @param flags one access mode, plus creation and status flags.
 * @param ... with O_CREAT, the mode_t for a new file.
 * @return the lowest free file descriptor, or -1 (errno set).
 */
int open(const char *path, int flags, ...);

/**
 * create a file, or truncate it if it exists, and open it for writing:
 * open(path, O_WRONLY | O_CREAT | O_TRUNC, mode).
 * @return a file descriptor, or -1 (errno set).
 */
int creat(const char *path, mode_t mode);

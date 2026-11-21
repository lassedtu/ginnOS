#pragma once

#include <fcntl.h>
#include <sys/wait.h>

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
 * close a file descriptor.
 * @param fd file descriptor to close.
 * @return 0 on success, -1 on failure.
 */
int close(int fd);

/**
 * execute a program, spawning a child process.
 * @param path path to the ELF executable.
 * @param argv null-terminated array of argument strings (may be NULL).
 * @return child PID on success, -1 on failure.
 */
pid_t exec(const char *path, const char **argv);

/**
 * create a child process that is a copy of the caller. the two share
 * nothing after the call: memory is copied (lazily, on the first write),
 * and the child gets its own copy of every fd.
 * @return the child's PID in the parent, 0 in the child, -1 on failure
 *         (errno set).
 */
pid_t fork(void);

/**
 * replace the calling process's program. the pid, cwd and open fds stay.
 * @param path path to the ELF executable.
 * @param argv NULL-terminated argument strings.
 * @param envp NULL-terminated "KEY=VALUE" environment strings.
 * @return only on failure: -1 (errno set).
 */
int execve(const char *path, char *const argv[], char *const envp[]);

/**
 * terminate the current process.
 * @param status exit code.
 */
void _exit(int status) __attribute__((noreturn));

/**
 * duplicate a descriptor to the lowest free one; both share the open file.
 * @return the new descriptor, or -1 (errno set).
 */
int dup(int fd);

// access() modes
#define F_OK 0 // the file exists
#define X_OK 1
#define W_OK 2
#define R_OK 4

/**
 * check that a file exists and may be accessed. there are no users yet,
 * so every existing file allows every access.
 * @param path the file.
 * @param mode F_OK, or R_OK | W_OK | X_OK.
 * @return 0, or -1 (errno set).
 */
int access(const char *path, int mode);

/**
 * read or write at an offset, without moving the file offset.
 * @return bytes transferred, or -1 (errno set; ESPIPE for a pipe).
 */
ssize_t pread(int fd, void *buf, size_t count, int offset);
ssize_t pwrite(int fd, const void *buf, size_t count, int offset);

/**
 * write an open file's data to disk.
 * @return 0, or -1 (errno set).
 */
int fsync(int fd);

/**
 * send SIGALRM to the calling process after a number of seconds.
 * @param seconds 0 cancels the alarm.
 * @return the seconds left of the previous alarm, or 0.
 */
unsigned int alarm(unsigned int seconds);

/**
 * sleep until a signal is caught. always returns -1 with EINTR.
 */
int pause(void);

/**
 * process groups and sessions (job control).
 * setpgid(pid, pgid): pid 0 is the caller, pgid 0 makes a new group led
 * by pid. getpgrp() is getpgid(0). setsid() starts a session and returns
 * its id.
 * @return as named, or -1 (errno set).
 */
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgid(pid_t pid);
pid_t getpgrp(void);
pid_t setsid(void);
pid_t getsid(pid_t pid);

/**
 * get the parent's process ID.
 * @return the parent's PID, or 0 if the parent has exited.
 */
pid_t getppid(void);

/**
 * let other ready processes run before this one continues.
 * @return 0.
 */
int sched_yield(void);

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

/**
 * get the current working directory.
 * @param buf buffer to write the path into.
 * @param size size of the buffer.
 * @return 0 on success, -1 on failure.
 */
int getcwd(char *buf, size_t size);

/**
 * change the current working directory.
 * @param path path to change to.
 * @return 0 on success, -1 on failure.
 */
int chdir(const char *path);

/**
 * remove (unlink) a file.
 * @param path path to the file to remove.
 * @return 0 on success, -1 on failure.
 */
int unlink(const char *path);

/**
 * remove a directory.
 * @param path path to the directory to remove.
 * @return 0 on success, -1 on failure.
 */
int rmdir(const char *path);

/**
 * create a file, or truncate it if it exists (the file is not left open).
 * @param path path to the file to create.
 * @return 0 on success, -1 on failure.
 */
int create(const char *path);

/**
 * create a directory.
 * @param path path to the directory to create.
 * @return 0 on success, -1 on failure.
 */
int mkdir(const char *path);

/* terminal mode constants */
#define TTY_COOKED 0
#define TTY_RAW    1

/**
 * switch terminal mode for the calling process.
 * @param mode TTY_COOKED (0) for line-buffered or TTY_RAW (1) for event-based.
 * @return the previous mode, or -1 on error.
 */
int ttyctl(int mode);

/**
 * issue a device-control request on an open file descriptor.
 * only device nodes under /dev service ioctls; other fds return -1/ENOTTY.
 * @param fd open file descriptor (e.g. an open "/dev/fb0").
 * @param request driver-defined request code.
 * @param arg request-specific argument, usually a pointer to a struct.
 * @return 0 (or a request-specific value) on success, -1 on error (errno set).
 */
int ioctl(int fd, unsigned int request, void *arg);

/* keyboard event types (matches kernel keyboard_event_type_t) */
#define KEY_EVENT_CHAR    0
#define KEY_EVENT_SPECIAL 1

/* special key codes (matches kernel keyboard_special_key_t) */
#define KEY_ARROW_UP    1
#define KEY_ARROW_DOWN  2
#define KEY_ARROW_LEFT  3
#define KEY_ARROW_RIGHT 4
#define KEY_HOME        5
#define KEY_END         6
#define KEY_PAGE_UP     7
#define KEY_PAGE_DOWN   8
#define KEY_INSERT      9
#define KEY_DELETE       10

/**
 * keyboard event structure (matches kernel keyboard_event_t).
 * in raw mode, read() on stdin fills this struct.
 */
typedef struct
{
    int type;          // KEY_EVENT_CHAR or KEY_EVENT_SPECIAL
    union
    {
        char character;  // valid when type == KEY_EVENT_CHAR
        int  special;    // valid when type == KEY_EVENT_SPECIAL
    };
} key_event_t;

/**
 * read a single keyboard event from stdin (raw mode).
 * blocks until an event is available.
 * @param event pointer to event struct to fill.
 * @return 0 on success, -1 on error.
 */
int read_event(key_event_t *event);

/**
 * create a pipe (pair of connected file descriptors).
 * @param fds array of two ints: fds[0] = read end, fds[1] = write end.
 * @return 0 on success, -1 on failure.
 */
int pipe(int fds[2]);

/**
 * duplicate a file descriptor to a specific number.
 * if new_fd is already open, it is closed first.
 * @param old_fd the fd to duplicate.
 * @param new_fd the target fd number.
 * @return new_fd on success, -1 on failure.
 */
int dup2(int old_fd, int new_fd);

/**
 * truncate an open file to zero length.
 * @param fd file descriptor.
 * @return 0 on success, -1 on failure.
 */
int ftruncate(int fd);

/* lseek whence constants */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/**
 * set file cursor position.
 * @param fd file descriptor.
 * @param offset byte offset.
 * @param whence SEEK_SET (from start), SEEK_CUR (from current), SEEK_END (from end).
 * @return new cursor position, or -1 on error.
 */
int lseek(int fd, int offset, int whence);

#pragma once

/**
 * per-process file descriptor tables. a descriptor is a pointer to an
 * open-file description (kernel/vfs/file.h) plus its own flags; several
 * descriptors (in one process after dup, or in several after fork) can
 * point to the same description.
 */

#include "common/stdint.h"
#include "kernel/vfs/file.h"

struct process;

// maximum number of file descriptors per process.
#define FD_MAX 64

// number of standard I/O descriptors (stdin, stdout, stderr).
#define FD_STDIO_COUNT 3

// descriptor flags (fcntl F_GETFD / F_SETFD)
#define FD_CLOEXEC 1 // closed by execve

/**
 * a single file descriptor entry.
 */
typedef struct
{
    file_t *file;   // the open-file description, or NULL if the slot is free
    uint32_t flags; // FD_CLOEXEC
} fd_entry_t;

/**
 * the descriptors of a process. the threads of a process use one table
 * together; it is freed when the last of them lets it go.
 */
typedef struct fd_table
{
    uint32_t users;        // the processes (threads) that use it
    fd_entry_t fd[FD_MAX]; // indexed by descriptor number
} fd_table_t;

/**
 * make a table with every descriptor free and one user.
 * @return the table, or NULL if there is no memory.
 */
fd_table_t *fd_table_create(void);

/**
 * add a user to a table (a new thread that shares it).
 * @return table.
 */
fd_table_t *fd_table_get(fd_table_t *table);

/**
 * remove a user from a table. the last user closes every descriptor (this
 * may sleep) and frees it.
 * @param table the table, or NULL (nothing is done).
 */
void fd_table_put(fd_table_t *table);

/**
 * put a description in the lowest free descriptor of a process, at or
 * above min_fd. the descriptor takes over the caller's reference.
 * @param proc the process.
 * @param file the description.
 * @param fd_flags FD_CLOEXEC or 0.
 * @param min_fd the lowest descriptor to use.
 * @return the descriptor, or -EMFILE if none is free (the caller keeps its
 *         reference then).
 */
int fd_install(struct process *proc, file_t *file, uint32_t fd_flags, int min_fd);

/**
 * get the entry for a descriptor of the current process.
 * @param fd the file descriptor number.
 * @return the entry, or NULL if fd is out of range or not open.
 */
fd_entry_t *fd_get(int fd);

/**
 * close a descriptor of the current process.
 * @param fd the file descriptor number.
 * @return 0, or -EBADF if fd is not open.
 */
int fd_close(int fd);

/**
 * close the description behind an entry and mark the entry free. works on
 * any process's table, so it can tear down a process that isn't current.
 * may sleep (the last close of a file takes the fs mutex).
 * @param entry the entry; a free entry is left as is.
 */
void fd_close_entry(fd_entry_t *entry);

/**
 * make dst another descriptor for src's description (fork, spawn). the
 * flags are copied too. dst must be free.
 * @param dst the entry to fill.
 * @param src the entry to duplicate.
 */
void fd_copy_entry(fd_entry_t *dst, const fd_entry_t *src);

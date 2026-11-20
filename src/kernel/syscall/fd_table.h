#pragma once

#include "common/stdint.h"
#include "kernel/vfs/vfs.h"
#include "kernel/syscall/pipe.h"

// maximum number of file descriptors per process.
#define FD_MAX 16

// number of standard I/O descriptors (stdin, stdout, stderr).
#define FD_STDIO_COUNT 3

/**
 * enumeration of file descriptor types.
 */
typedef enum
{
    FD_TYPE_NONE = 0, // slot is unused
    FD_TYPE_CONSOLE,  // stdin/stdout/stderr backed by keyboard + VGA
    FD_TYPE_FILE,     // regular file or directory backed by VFS
    FD_TYPE_PIPE,     // pipe endpoint (read or write)
} fd_type_t;

/**
 * a single file descriptor entry.
 */
typedef struct
{
    fd_type_t type;
    union
    {
        vfs_file_t file; /* valid when type == FD_TYPE_FILE */
        struct
        {
            pipe_buf_t *buf; /* shared pipe buffer */
            pipe_dir_t dir;  /* read or write end */
        } pipe;              /* valid when type == FD_TYPE_PIPE */
    };
} fd_entry_t;

/**
 * initialize the file descriptor layer (the pipe pool).
 */
void fd_table_init(void);

/**
 * allocate a file descriptor for an open VFS file.
 * @param file pointer to a vfs_file_t to copy into the table.
 * @return fd number (>= 0) on success, -1 if the table is full.
 */
int fd_alloc(vfs_file_t *file);

/**
 * get the fd entry for a given descriptor number.
 * @param fd the file descriptor number.
 * @return pointer to the entry, or NULL if fd is invalid or unused.
 */
fd_entry_t *fd_get(int fd);

/**
 * close and free a file descriptor.
 * if the fd is a VFS file, it is closed via vfs_close().
 * if the fd is a pipe end, that end is closed (see pipe_unref()).
 * @param fd the file descriptor number.
 * @return 0 on success, -1 if fd is invalid.
 */
int fd_free(int fd);

/**
 * close the object behind an fd entry and mark the entry unused. works on
 * any process's table, so it can tear down a process that isn't current.
 * @param entry the entry to close; an unused entry is left as is.
 */
void fd_close_entry(fd_entry_t *entry);

/**
 * make dst another descriptor for the object behind src. a pipe end gains
 * a reference; a file entry is copied by value (no shared offset yet).
 * dst must be unused.
 * @param dst the entry to fill.
 * @param src the entry to duplicate.
 */
void fd_copy_entry(fd_entry_t *dst, const fd_entry_t *src);

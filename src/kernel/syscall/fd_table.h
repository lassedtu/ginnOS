#pragma once

#include "common/stdint.h"
#include "kernel/vfs/vfs.h"

// maximum number of file descriptors per process.
#define FD_MAX 16

// number of standard I/O descriptors (stdin, stdout, stderr).
#define FD_STDIO_COUNT 3

// pipe buffer size (4 KiB)
#define PIPE_BUF_SIZE 4096

// maximum number of simultaneous pipes system-wide
#define PIPE_MAX 16

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
 * pipe buffer. shared between read and write ends.
 */
typedef struct pipe_buf
{
    char data[PIPE_BUF_SIZE];
    uint32_t read_pos;  // next byte to read
    uint32_t write_pos; // next byte to write
    uint32_t count;     // bytes currently in buffer
    int read_refs;      // number of open read-end file descriptors
    int write_refs;     // number of open write-end file descriptors
    int ref_count;      // total fd entries referencing this pipe
} pipe_buf_t;

/**
 * pipe endpoint direction.
 */
typedef enum
{
    PIPE_READ = 0,
    PIPE_WRITE = 1,
} pipe_dir_t;

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
 * initialize the file descriptor table and pipe pool.
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
 * if the fd is a pipe end, decrements ref count and marks end as closed.
 * @param fd the file descriptor number.
 * @return 0 on success, -1 if fd is invalid.
 */
int fd_free(int fd);

/**
 * allocate a pipe buffer from the system pipe pool.
 * @return pointer to an initialized pipe buffer, or NULL if pool is exhausted.
 */
pipe_buf_t *pipe_alloc(void);

/**
 * release a pipe buffer back to the pool (when ref_count reaches 0).
 * @param buf the pipe buffer to release.
 */
void pipe_release(pipe_buf_t *buf);

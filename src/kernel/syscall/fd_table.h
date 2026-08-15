#pragma once

#include "../../common/stdint.h"
#include "../vfs/vfs.h"

// maximum number of file descriptors per process.
#define FD_MAX 16

/**
 * enumeration of file descriptor types.
 */
typedef enum
{
    FD_TYPE_NONE = 0, // slot is unused
    FD_TYPE_CONSOLE,  // stdin/stdout/stderr backed by keyboard + VGA
    FD_TYPE_FILE,     // regular file or directory backed by VFS
} fd_type_t;

/**
 * a single file descriptor entry.
 */
typedef struct
{
    fd_type_t type;
    VFS_FILE file; /* only valid when type == FD_TYPE_FILE */
} fd_entry_t;

/**
 * initialize the file descriptor table.
 * pre-opens fd 0 (stdin), 1 (stdout), 2 (stderr) as console descriptors.
 */
void fd_table_init(void);

/**
 * allocate a file descriptor for an open VFS file.
 * @param file pointer to a VFS_FILE to copy into the table.
 * @return fd number (>= 0) on success, -1 if the table is full.
 */
int fd_alloc(VFS_FILE *file);

/**
 * get the fd entry for a given descriptor number.
 * @param fd the file descriptor number.
 * @return pointer to the entry, or NULL if fd is invalid or unused.
 */
fd_entry_t *fd_get(int fd);

/**
 * close and free a file descriptor.
 * if the fd is a VFS file, it is closed via vfs_close().
 * @param fd the file descriptor number.
 * @return 0 on success, -1 if fd is invalid.
 */
int fd_free(int fd);

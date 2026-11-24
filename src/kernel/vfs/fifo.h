#pragma once

/**
 * @file fifo.h
 * @brief named pipes (FIFOs): a pipe that processes reach by a name.
 *
 * all who open the same named pipe share one pipe. it lives while one of
 * them has it open; then the next open starts an empty one. opening one
 * end waits until the other end is open too, unless O_NONBLOCK is given
 * (then a read end opens at once, and a write end with no reader fails
 * with ENXIO). O_RDWR opens both ends and does not wait.
 */

#include "common/stdint.h"
#include "kernel/fs/fs.h"
#include "kernel/vfs/file.h"

/**
 * open the named pipe that is inode on a mount.
 * @param flags the open flags (the access mode and O_NONBLOCK).
 * @param out gets the open file.
 * @return 0, or -ENXIO, -ERESTARTSYS (a signal while waiting), -ENOMEM,
 *         -ENFILE (too many named pipes open at once).
 */
int32_t fifo_open(fs_mount_t *mount, uint32_t inode, uint32_t flags, file_t **out);

/**
 * close an end of a named pipe (the file's release).
 */
void fifo_close(file_t *file);

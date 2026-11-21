#pragma once

/**
 * @file procfs.h
 * @brief a read-only view of kernel state, mounted at /proc.
 *
 * /proc/meminfo, /proc/uptime, /proc/<pid>/status and /proc/self (see
 * procfs.c).
 */

#include "common/error.h"
#include "kernel/fs/fs.h"

/**
 * set up procfs on a mount. hand it to vfs_mount afterwards.
 * @param mount filesystem mount object to initialize.
 * @return KERR_OK.
 */
kerr_t procfs_mount(fs_mount_t *mount);

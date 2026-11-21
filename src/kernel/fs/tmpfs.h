#pragma once

/**
 * @file tmpfs.h
 * @brief a filesystem in kernel memory, mounted at /tmp.
 *
 * files and directories live on the kernel heap (8 MiB at most in all) and
 * are lost at reboot. there is one tmpfs.
 */

#include "common/error.h"
#include "kernel/fs/fs.h"

/**
 * set up the tmpfs (empty) on a mount. hand it to vfs_mount afterwards.
 * @param mount filesystem mount object to initialize.
 * @return KERR_OK.
 */
kerr_t tmpfs_mount(fs_mount_t *mount);

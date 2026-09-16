#pragma once

#include "common/error.h"
#include "drivers/disk/block_device.h"

struct fs_mount;
typedef struct fs_mount fs_mount_t;

/**
 * initialize an ext2 filesystem on a block device and install its operations
 * vtable on the mount. after this succeeds, all access goes through mount->ops.
 * @param mount filesystem mount object to initialize.
 * @param device initialized block device backend.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t ext2_ops_mount(fs_mount_t *mount, block_device_t *device);

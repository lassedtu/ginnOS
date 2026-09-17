#pragma once

/**
 * @file devfs.h
 * @brief a small read-only pseudo-filesystem that surfaces the device registry.
 *
 * devfs has no backing block device: its "files" are the devices registered in
 * the kernel device model (see src/kernel/device/device.h). mounting it at
 * "/dev" makes `ls /dev` enumerate registered devices (fb0, tty0, hda, ...) and
 * lets a program open a device node like "/dev/fb0" by name.
 *
 * it is deliberately minimal for now: the directory listing and opening a node
 * by name work; byte read/write and ioctl/mmap on a node are added per device
 * family (the Linux-style /dev/fb0 fbdev is the first, see framebuffer-plan.md).
 * the namespace is flat (no sub-directories) and read-only (no create/remove).
 */

#include "common/error.h"
#include "kernel/fs/fs.h"

/**
 * initialize a mount to be backed by devfs.
 *
 * unlike fs_mount(), devfs has no block device to probe: it reads the live
 * device registry on demand. after this returns KERR_OK, hand @p mount to
 * vfs_mount("/dev", mount).
 * @param mount filesystem mount object to initialize.
 * @return KERR_OK on success.
 */
kerr_t devfs_mount(fs_mount_t *mount);

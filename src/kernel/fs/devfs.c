#include "devfs.h"

#include "common/string.h"
#include "kernel/device/device.h"

/**
 * @file devfs.c
 * @brief read-only pseudo-filesystem backing the device registry.
 *
 * implements the fs_ops_t vtable so the generic fs_* layer can dispatch to it
 * exactly like ext2. paths arriving here are already mount-relative (the VFS
 * strips the "/dev" prefix, see vfs_resolve_mount_path): the directory root is
 * "/", and a device node is "/name" (e.g. "/fb0").
 *
 * state lives in the generic fs_file fields added for non-ext2 filesystems:
 *   - fs_data: the target device_t* for an open node (NULL for the root dir)
 *   - fs_pos:  the next registry index to yield from read_entry (root dir)
 */

/**
 * strip the single leading '/' from a mount-relative devfs path, giving the
 * bare device name. "/fb0" -> "fb0"; "/" -> "" (the directory root).
 */
static const char *devfs_leaf(const char *path)
{
    if (path[0] == '/')
    {
        return path + 1;
    }
    return path;
}

static kerr_t devfs_open(fs_mount_t *mount, const char *path, fs_file_t *file)
{
    (void)mount;

    const char *name = devfs_leaf(path);

    // empty leaf means the "/dev" directory itself.
    if (name[0] == '\0')
    {
        file->fs_data = NULL;
        file->fs_pos = 0;
        file->file_type = FS_TYPE_DIR;
        return KERR_OK;
    }

    device_t *dev = device_find(name);
    if (!dev)
    {
        return KERR_NOENT;
    }

    // a device node is a leaf "file"; the concrete device is remembered so a
    // later read/write/ioctl can route to it (per device family).
    file->fs_data = dev;
    file->fs_pos = 0;
    file->file_type = FS_TYPE_FILE;
    return KERR_OK;
}

static kerr_t devfs_stat(fs_mount_t *mount, const char *path, fs_stat_t *stat_out)
{
    (void)mount;

    const char *name = devfs_leaf(path);

    // zero the stat first; devfs has no real inodes, sizes, or timestamps.
    stat_out->inode = 0;
    stat_out->mode = 0;
    stat_out->links_count = 1;
    stat_out->size = 0;
    stat_out->blocks = 0;
    stat_out->atime = 0;
    stat_out->mtime = 0;
    stat_out->ctime = 0;

    if (name[0] == '\0')
    {
        stat_out->file_type = FS_TYPE_DIR;
        return KERR_OK;
    }

    if (!device_find(name))
    {
        return KERR_NOENT;
    }

    stat_out->file_type = FS_TYPE_FILE;
    return KERR_OK;
}

static kerr_t devfs_read_entry(fs_file_t *file, fs_dirent_t *entry_out)
{
    // only the directory root enumerates; a device node has no children.
    if (file->file_type != FS_TYPE_DIR)
    {
        return KERR_INVAL;
    }

    device_t *dev = device_get(file->fs_pos);
    if (!dev)
    {
        return KERR_NOENT; // past the last device: end of directory.
    }

    file->fs_pos++;

    entry_out->inode = file->fs_pos; // synthetic, 1-based; devfs has no inodes.
    entry_out->file_type = FS_TYPE_FILE;
    entry_out->size = 0;
    strncpy(entry_out->name, dev->name, sizeof(entry_out->name) - 1);
    entry_out->name[sizeof(entry_out->name) - 1] = '\0';
    return KERR_OK;
}

// devfs is read-only for now: byte i/o and mutations are per-device-family
// work (the fbdev in FB6 wires read/write/ioctl/mmap on /dev/fb0). until then
// these are safe no-ops / errors rather than touching a device blindly.

static uint32_t devfs_read(fs_file_t *file, uint32_t byte_count, void *data_out)
{
    (void)file;
    (void)byte_count;
    (void)data_out;
    return 0;
}

static uint32_t devfs_write(fs_file_t *file, uint32_t byte_count, const void *data_in)
{
    (void)file;
    (void)byte_count;
    (void)data_in;
    return 0;
}

static kerr_t devfs_truncate(fs_file_t *file)
{
    (void)file;
    return KERR_PERM;
}

static kerr_t devfs_create(fs_mount_t *mount, const char *path)
{
    (void)mount;
    (void)path;
    return KERR_PERM;
}

static kerr_t devfs_mkdir(fs_mount_t *mount, const char *path)
{
    (void)mount;
    (void)path;
    return KERR_PERM;
}

static kerr_t devfs_remove(fs_mount_t *mount, const char *path)
{
    (void)mount;
    (void)path;
    return KERR_PERM;
}

static kerr_t devfs_rmdir(fs_mount_t *mount, const char *path)
{
    (void)mount;
    (void)path;
    return KERR_PERM;
}

static kerr_t devfs_rename(fs_mount_t *mount, const char *old_path, const char *new_path)
{
    (void)mount;
    (void)old_path;
    (void)new_path;
    return KERR_PERM;
}

static void devfs_close(fs_file_t *file)
{
    file->fs_data = NULL;
}

// the devfs operations table handed to the generic fs layer.
static const fs_ops_t devfs_ops = {
    .open = devfs_open,
    .create = devfs_create,
    .mkdir = devfs_mkdir,
    .remove = devfs_remove,
    .rmdir = devfs_rmdir,
    .rename = devfs_rename,
    .stat = devfs_stat,
    .read = devfs_read,
    .write = devfs_write,
    .truncate = devfs_truncate,
    .read_entry = devfs_read_entry,
    .close = devfs_close,
};

kerr_t devfs_mount(fs_mount_t *mount)
{
    if (!mount)
    {
        return KERR_INVAL;
    }

    mount->ops = &devfs_ops;
    mount->fs_data = NULL; // devfs reads the live registry; no per-mount state.
    mount->is_mounted = 1;
    return KERR_OK;
}

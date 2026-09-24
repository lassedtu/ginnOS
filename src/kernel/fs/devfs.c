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

// devfs is read-only for filesystem structure, but a device node can carry
// byte i/o: read/write forward to the backing device at the file's cursor
// (fs_pos), which advances by the transferred count. mutations to the devfs
// namespace itself (create/mkdir/...) stay forbidden.

static uint32_t devfs_read(fs_file_t *file, uint32_t byte_count, void *data_out)
{
    device_t *dev = (device_t *)file->fs_data;
    if (!dev || !dev->ops || !dev->ops->read)
    {
        return 0;
    }

    uint32_t n = dev->ops->read(dev, file->fs_pos, data_out, byte_count);
    file->fs_pos += n;
    return n;
}

static uint32_t devfs_write(fs_file_t *file, uint32_t byte_count, const void *data_in)
{
    device_t *dev = (device_t *)file->fs_data;
    if (!dev || !dev->ops || !dev->ops->write)
    {
        return 0;
    }

    uint32_t n = dev->ops->write(dev, file->fs_pos, data_in, byte_count);
    file->fs_pos += n;
    return n;
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

static int32_t devfs_ioctl(fs_file_t *file, uint32_t request, void *arg)
{
    // forward to the backing device's ioctl hook. fs_data holds the device_t*
    // stashed at open time; the directory root (fs_data == NULL) has none.
    device_t *dev = (device_t *)file->fs_data;
    if (!dev || !dev->ops || !dev->ops->ioctl)
    {
        return -25; /* ENOTTY: device supports no ioctls */
    }

    return dev->ops->ioctl(dev, request, arg);
}

static int32_t devfs_mmap(fs_file_t *file, uint32_t page_directory, uint32_t virt,
                          uint32_t length, int prot, uint32_t offset)
{
    // forward to the backing device's mmap hook (e.g. the framebuffer maps its
    // LFB). a device without one is not mappable.
    device_t *dev = (device_t *)file->fs_data;
    if (!dev || !dev->ops || !dev->ops->mmap)
    {
        return -19; /* ENODEV: device is not mappable */
    }

    return dev->ops->mmap(dev, page_directory, virt, length, prot, offset);
}

static int32_t devfs_seek(fs_file_t *file, int32_t offset, int whence)
{
    device_t *dev = (device_t *)file->fs_data;
    uint32_t size = (dev && dev->ops && dev->ops->size) ? dev->ops->size(dev) : 0;

    int32_t base;
    switch (whence)
    {
    case 0: /* SEEK_SET */
        base = 0;
        break;
    case 1: /* SEEK_CUR */
        base = (int32_t)file->fs_pos;
        break;
    case 2: /* SEEK_END */
        base = (int32_t)size;
        break;
    default:
        return -22; /* EINVAL */
    }

    int32_t new_pos = base + offset;
    if (new_pos < 0)
    {
        new_pos = 0;
    }

    file->fs_pos = (uint32_t)new_pos;
    return new_pos;
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
    .ioctl = devfs_ioctl,
    .mmap = devfs_mmap,
    .seek = devfs_seek,
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

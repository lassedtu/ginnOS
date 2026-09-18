#include "fs.h"
#include "ext2_ops.h"

/**
 * @file fs.c
 * @brief generic filesystem layer.
 *
 * fs_* dispatches through the mounted filesystem's operations vtable
 * (fs_ops_t) and never names a concrete filesystem. fs_mount() is the one
 * place that picks a filesystem implementation; today that is always ext2,
 * but adding another means providing its own fs_ops_t and selecting it here.
 */

bool fs_mount(fs_mount_t *mount, block_device_t *device)
{
    if (!mount || !device)
    {
        return false;
    }

    // ext2 is the only filesystem for now; this is where a future mount would
    // probe the device and choose between ext2/FAT32/etc.
    if (kerr_failed(ext2_ops_mount(mount, device)))
    {
        return false;
    }

    mount->is_mounted = 1;
    return true;
}

kerr_t fs_open(fs_mount_t *mount, const char *path, fs_file_t *file)
{
    kerr_t err;

    if (!mount || !file || !path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    err = mount->ops->open(mount, path, file);
    if (kerr_failed(err))
    {
        return err;
    }

    // remember which filesystem this file lives on so file-level calls
    // dispatch without needing the mount again.
    file->ops = mount->ops;
    file->is_open = 1;
    return KERR_OK;
}

kerr_t fs_create(fs_mount_t *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return mount->ops->create(mount, path);
}

kerr_t fs_mkdir(fs_mount_t *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return mount->ops->mkdir(mount, path);
}

kerr_t fs_remove(fs_mount_t *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return mount->ops->remove(mount, path);
}

kerr_t fs_rmdir(fs_mount_t *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return mount->ops->rmdir(mount, path);
}

kerr_t fs_rename(fs_mount_t *mount, const char *old_path, const char *new_path)
{
    if (!mount || !old_path || !new_path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return mount->ops->rename(mount, old_path, new_path);
}

kerr_t fs_stat(fs_mount_t *mount, const char *path, fs_stat_t *stat_out)
{
    if (!mount || !path || !stat_out || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return mount->ops->stat(mount, path, stat_out);
}

uint32_t fs_read(fs_file_t *file, uint32_t byteCount, void *dataOut)
{
    if (!file || !file->is_open)
    {
        return 0;
    }

    return file->ops->read(file, byteCount, dataOut);
}

uint32_t fs_write(fs_file_t *file, uint32_t byteCount, const void *dataIn)
{
    if (!file || !file->is_open)
    {
        return 0;
    }

    return file->ops->write(file, byteCount, dataIn);
}

kerr_t fs_truncate(fs_file_t *file)
{
    if (!file || !file->is_open)
    {
        return KERR_INVAL;
    }

    return file->ops->truncate(file);
}

kerr_t fs_read_entry(fs_file_t *file, fs_dirent_t *entryOut)
{
    if (!file || !entryOut || !file->is_open)
    {
        return KERR_INVAL;
    }

    return file->ops->read_entry(file, entryOut);
}

void fs_close(fs_file_t *file)
{
    if (!file || !file->is_open)
    {
        return;
    }

    file->ops->close(file);
    file->is_open = 0;
    file->file_type = FS_TYPE_UNKNOWN;
}

uint8_t fs_file_type(const fs_file_t *file)
{
    if (!file)
    {
        return FS_TYPE_UNKNOWN;
    }

    return file->file_type;
}

int32_t fs_ioctl(fs_file_t *file, uint32_t request, void *arg)
{
    if (!file || !file->is_open)
    {
        return -9; /* EBADF */
    }

    // a filesystem without an ioctl op supports no device control: -ENOTTY,
    // the same "inappropriate ioctl for device" errno a regular file returns.
    if (!file->ops->ioctl)
    {
        return -25; /* ENOTTY */
    }

    return file->ops->ioctl(file, request, arg);
}

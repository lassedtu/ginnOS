#include "vfs.h"

#include "common/string.h"

extern FS_MOUNT *root_mount;

kerr_t vfs_open(
    const char *path,
    VFS_FILE *file)
{
    if (!root_mount || !file || !path)
        return KERR_INVAL;

    kerr_t err = fs_open(
            root_mount,
            path,
            &file->file);
    if (kerr_failed(err))
    {
        file->mount = 0;
        return err;
    }

    file->mount = root_mount;
    return KERR_OK;
}

kerr_t vfs_create(const char *path)
{
    if (!root_mount || !path)
    {
        return KERR_INVAL;
    }

    return fs_create(root_mount, path);
}

kerr_t vfs_mkdir(const char *path)
{
    if (!root_mount || !path)
    {
        return KERR_INVAL;
    }

    return fs_mkdir(root_mount, path);
}

kerr_t vfs_remove(const char *path)
{
    if (!root_mount || !path)
    {
        return KERR_INVAL;
    }

    return fs_remove(root_mount, path);
}

kerr_t vfs_rmdir(const char *path)
{
    if (!root_mount || !path)
    {
        return KERR_INVAL;
    }

    return fs_rmdir(root_mount, path);
}

kerr_t vfs_rename(const char *old_path, const char *new_path)
{
    if (!root_mount || !old_path || !new_path)
    {
        return KERR_INVAL;
    }

    return fs_rename(root_mount, old_path, new_path);
}

kerr_t vfs_stat(const char *path, VFS_STAT *stat_out)
{
    if (!root_mount || !path || !stat_out)
    {
        return KERR_INVAL;
    }

    return fs_stat(root_mount, path, stat_out);
}

uint32_t vfs_read(
    VFS_FILE *file,
    uint32_t size,
    void *buffer)
{
    if (!file || !file->mount || !buffer)
    {
        return 0;
    }

    return fs_read(
        &file->file,
        size,
        buffer);
}

uint32_t vfs_write(
    VFS_FILE *file,
    uint32_t size,
    const void *buffer)
{
    if (!file || !file->mount || !buffer)
    {
        return 0;
    }

    return fs_write(
        &file->file,
        size,
        buffer);
}

kerr_t vfs_truncate(VFS_FILE *file)
{
    if (!file || !file->mount)
    {
        return KERR_INVAL;
    }

    return fs_truncate(&file->file);
}

kerr_t vfs_read_entry(
    VFS_FILE *file,
    FS_DIRENT *entryOut)
{
    if (!file || !file->mount || !entryOut)
    {
        return KERR_INVAL;
    }

    return fs_read_entry(&file->file, entryOut);
}

void vfs_close(
    VFS_FILE *file)
{
    if (!file)
    {
        return;
    }

    fs_close(&file->file);
    file->mount = 0;
}

uint8_t vfs_file_type(
    VFS_FILE *file)
{
    if (!file || !file->mount)
    {
        return FS_TYPE_UNKNOWN;
    }

    return fs_file_type(&file->file);
}

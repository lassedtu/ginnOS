#include "vfs.h"

static FS_MOUNT *root_mount = 0;

bool vfs_mount_root(FS_MOUNT *mount)
{
    if (!mount || !mount->is_mounted)
    {
        return false;
    }

    root_mount = mount;

    return true;
}

bool vfs_open(
    const char *path,
    VFS_FILE *file)
{
    if (!root_mount || !file || !path)
        return false;

    if (!fs_open(
            root_mount,
            path,
            &file->file))
    {
        file->mount = 0;
        return false;
    }

    file->mount = root_mount;
    return true;
}

uint32_t vfs_read(
    VFS_FILE *file,
    uint32_t size,
    void *buffer)
{
    if (!file || !file->mount)
    {
        return 0;
    }

    return fs_read(
        &file->file,
        size,
        buffer);
}

bool vfs_read_entry(
    VFS_FILE *file,
    FS_DIRENT *entryOut)
{
    if (!file || !file->mount || !entryOut)
    {
        return false;
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

uint8_t vfs_type(
    VFS_FILE *file)
{
    return vfs_file_type(file);
}
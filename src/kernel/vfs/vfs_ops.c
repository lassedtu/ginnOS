#include "vfs.h"
#include "kernel/errno.h"

#include "common/string.h"

kerr_t vfs_open(
    const char *path,
    vfs_file_t *file)
{
    if (!file || !path)
        return KERR_INVAL;

    const char *rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(path, &rel);
    if (!mount)
        return KERR_INVAL;

    kerr_t err = fs_open(
            mount,
            rel,
            &file->file);
    if (kerr_failed(err))
    {
        file->mount = NULL;
        return err;
    }

    file->mount = mount;
    return KERR_OK;
}

kerr_t vfs_create(const char *path)
{
    if (!path)
    {
        return KERR_INVAL;
    }

    const char *rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(path, &rel);
    if (!mount)
    {
        return KERR_INVAL;
    }

    return fs_create(mount, rel);
}

kerr_t vfs_mkdir(const char *path)
{
    if (!path)
    {
        return KERR_INVAL;
    }

    const char *rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(path, &rel);
    if (!mount)
    {
        return KERR_INVAL;
    }

    return fs_mkdir(mount, rel);
}

kerr_t vfs_remove(const char *path)
{
    if (!path)
    {
        return KERR_INVAL;
    }

    const char *rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(path, &rel);
    if (!mount)
    {
        return KERR_INVAL;
    }

    return fs_remove(mount, rel);
}

kerr_t vfs_rmdir(const char *path)
{
    if (!path)
    {
        return KERR_INVAL;
    }

    const char *rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(path, &rel);
    if (!mount)
    {
        return KERR_INVAL;
    }

    return fs_rmdir(mount, rel);
}

kerr_t vfs_rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path)
    {
        return KERR_INVAL;
    }

    const char *old_rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(old_path, &old_rel);
    if (!mount)
    {
        return KERR_INVAL;
    }

    // rename across different mounts is not supported: both paths must
    // resolve to the same filesystem. resolve the destination's relative path
    // too so the backing fs sees mount-relative names for both.
    const char *new_rel = NULL;
    if (vfs_resolve_mount_path(new_path, &new_rel) != mount)
    {
        return KERR_INVAL;
    }

    return fs_rename(mount, old_rel, new_rel);
}

kerr_t vfs_stat(const char *path, vfs_stat_t *stat_out)
{
    if (!path || !stat_out)
    {
        return KERR_INVAL;
    }

    const char *rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(path, &rel);
    if (!mount)
    {
        return KERR_INVAL;
    }

    return fs_stat(mount, rel, stat_out);
}

uint32_t vfs_read(
    vfs_file_t *file,
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
    vfs_file_t *file,
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

kerr_t vfs_truncate(vfs_file_t *file)
{
    if (!file || !file->mount)
    {
        return KERR_INVAL;
    }

    return fs_truncate(&file->file);
}

kerr_t vfs_read_entry(
    vfs_file_t *file,
    fs_dirent_t *entryOut)
{
    if (!file || !file->mount || !entryOut)
    {
        return KERR_INVAL;
    }

    return fs_read_entry(&file->file, entryOut);
}

void vfs_close(
    vfs_file_t *file)
{
    if (!file)
    {
        return;
    }

    fs_close(&file->file);
    file->mount = NULL;
}

uint8_t vfs_file_type(
    vfs_file_t *file)
{
    if (!file || !file->mount)
    {
        return FS_TYPE_UNKNOWN;
    }

    return fs_file_type(&file->file);
}

int32_t vfs_ioctl(vfs_file_t *file, uint32_t request, void *arg)
{
    if (!file || !file->mount)
    {
        return -EBADF;
    }

    return fs_ioctl(&file->file, request, arg);
}

int32_t vfs_mmap(vfs_file_t *file, addr_space_t as, uintptr_t virt,
                 size_t length, int prot, uint32_t offset)
{
    if (!file || !file->mount)
    {
        return -EBADF;
    }

    return fs_mmap(&file->file, as, virt, length, prot, offset);
}

int32_t vfs_lseek(vfs_file_t *file, int32_t offset, int whence)
{
    if (!file || !file->mount)
    {
        return -EBADF;
    }

    // a filesystem with its own seek (devfs) moves its own cursor
    int32_t moved = fs_seek(&file->file, offset, whence);
    if (moved != -ENOSYS)
    {
        return moved;
    }

    // ext2: move the ext2 file cursor
    ext2_file_t *ext2 = &file->file.ext2_file;
    int32_t base;
    switch (whence)
    {
    case VFS_SEEK_SET:
        base = 0;
        break;
    case VFS_SEEK_CUR:
        base = (int32_t)ext2->cursor;
        break;
    case VFS_SEEK_END:
        base = (int32_t)ext2->size;
        break;
    default:
        return -EINVAL;
    }

    int32_t position = base + offset;
    if (position < 0)
    {
        return -EINVAL;
    }
    ext2->cursor = (uint32_t)position;
    return position;
}

void vfs_seek_end(vfs_file_t *file)
{
    vfs_lseek(file, 0, VFS_SEEK_END);
}

kerr_t vfs_fstat(vfs_file_t *file, vfs_stat_t *stat_out)
{
    if (!file || !file->mount)
    {
        return KERR_INVAL;
    }
    return fs_fstat(&file->file, stat_out);
}

kerr_t vfs_setattr(const char *path, const fs_attr_t *attr)
{
    vfs_file_t file;
    kerr_t err = vfs_open(path, &file);
    if (kerr_failed(err))
    {
        return err;
    }
    err = fs_setattr(&file.file, attr);
    vfs_close(&file);
    return err;
}

kerr_t vfs_fsetattr(vfs_file_t *file, const fs_attr_t *attr)
{
    if (!file || !file->mount)
    {
        return KERR_INVAL;
    }
    return fs_setattr(&file->file, attr);
}

#include "fs.h"
#include "ext2_ops.h"
#include "kernel/sync/mutex.h"

#include "common/memory.h"

/**
 * @file ext2_ops.c
 * @brief ext2 implementation of the filesystem operations vtable.
 *
 * this is the only file in the fs layer that names ext2 functions. it adapts
 * ext2's calls and types to the generic fs_* contract and exposes them as a
 * single fs_ops_t. fs_mount() installs this table; the generic fs.c dispatches
 * through it and never touches ext2 directly.
 */

/**
 * map an ext2 directory-entry file type to the generic FS file type.
 */
static uint8_t map_ext2_file_type(uint8_t ext2_type)
{
    if (ext2_type == EXT2_FT_REG_FILE)
    {
        return FS_TYPE_FILE;
    }

    if (ext2_type == EXT2_FT_DIR)
    {
        return FS_TYPE_DIR;
    }

    if (ext2_type == EXT2_FT_SYMLINK)
    {
        return FS_TYPE_SYMLINK;
    }

    return FS_TYPE_UNKNOWN;
}

/**
 * map an ext2 inode mode to the generic FS file type.
 */
static uint8_t map_inode_type(uint16_t mode)
{
    if ((mode & EXT2_S_IFMT) == EXT2_S_IFREG)
    {
        return FS_TYPE_FILE;
    }

    if ((mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
    {
        return FS_TYPE_DIR;
    }

    if ((mode & EXT2_S_IFMT) == EXT2_S_IFLNK)
    {
        return FS_TYPE_SYMLINK;
    }

    return FS_TYPE_UNKNOWN;
}

static kerr_t ext2_ops_open(fs_mount_t *mount, const char *path, fs_file_t *file)
{
    kerr_t err = ext2_open(&mount->ext2, path, &file->ext2_file);
    if (kerr_failed(err))
    {
        return err;
    }

    file->file_type = map_ext2_file_type(file->ext2_file.file_type);
    return KERR_OK;
}

static kerr_t ext2_ops_create(fs_mount_t *mount, const char *path)
{
    return ext2_create_file(&mount->ext2, path);
}

static kerr_t ext2_ops_mkdir(fs_mount_t *mount, const char *path)
{
    return ext2_create_dir(&mount->ext2, path);
}

static kerr_t ext2_ops_remove(fs_mount_t *mount, const char *path)
{
    return ext2_remove_file(&mount->ext2, path);
}

static kerr_t ext2_ops_rmdir(fs_mount_t *mount, const char *path)
{
    return ext2_remove_dir(&mount->ext2, path);
}

static kerr_t ext2_ops_rename(fs_mount_t *mount, const char *old_path, const char *new_path)
{
    return ext2_rename(&mount->ext2, old_path, new_path);
}

/**
 * fill a stat from an inode.
 */
static void fill_stat(uint32_t inode_number, const ext2_inode_t *inode, fs_stat_t *stat_out)
{
    stat_out->inode = inode_number;
    stat_out->file_type = map_inode_type(inode->i_mode);
    stat_out->mode = inode->i_mode;
    stat_out->links_count = inode->i_links_count;
    stat_out->size = inode->i_size;
    stat_out->blocks = inode->i_blocks;
    stat_out->atime = inode->i_atime;
    stat_out->mtime = inode->i_mtime;
    stat_out->ctime = inode->i_ctime;
    stat_out->uid = inode->i_uid;
    stat_out->gid = inode->i_gid;
}

static kerr_t ext2_ops_stat(fs_mount_t *mount, const char *path, fs_stat_t *stat_out)
{
    uint32_t inode_number;
    ext2_inode_t inode;
    kerr_t err;

    err = ext2_lookup_path(&mount->ext2, path, &inode_number);
    if (kerr_failed(err))
    {
        return err;
    }

    err = ext2_read_inode(&mount->ext2, inode_number, &inode);
    if (kerr_failed(err))
    {
        return err;
    }

    fill_stat(inode_number, &inode, stat_out);
    return KERR_OK;
}

static kerr_t ext2_ops_fstat(fs_file_t *file, fs_stat_t *stat_out)
{
    ext2_file_t *ext2 = &file->ext2_file;
    ext2_inode_t inode;

    kerr_t err = ext2_read_inode(ext2->volume, ext2->inode, &inode);
    if (kerr_failed(err))
    {
        return err;
    }

    fill_stat(ext2->inode, &inode, stat_out);
    return KERR_OK;
}

static uint32_t ext2_ops_read(fs_file_t *file, uint32_t byte_count, void *data_out)
{
    return ext2_read(&file->ext2_file, byte_count, data_out);
}

static uint32_t ext2_ops_write(fs_file_t *file, uint32_t byte_count, const void *data_in)
{
    return ext2_write(&file->ext2_file, byte_count, data_in);
}

static kerr_t ext2_ops_truncate(fs_file_t *file)
{
    ext2_truncate(&file->ext2_file);
    return KERR_OK;
}

static kerr_t ext2_ops_read_entry(fs_file_t *file, fs_dirent_t *entry_out)
{
    ext2_directory_entry_t ext2_entry;
    kerr_t err = ext2_read_entry(&file->ext2_file, &ext2_entry);
    if (kerr_failed(err))
    {
        return err;
    }

    entry_out->inode = ext2_entry.inode;
    entry_out->file_type = map_ext2_file_type(ext2_entry.file_type);
    entry_out->size = ext2_entry.size;
    memcpy(entry_out->name, ext2_entry.name, sizeof(entry_out->name));
    return KERR_OK;
}

static void ext2_ops_close(fs_file_t *file)
{
    ext2_close(&file->ext2_file);
}

/*
 * locking: the ext2 driver keeps per-volume scratch buffers and the ATA
 * driver below it drives one controller by programmed I/O, so neither is
 * reentrant. a process preempted inside either, with another process
 * entering the filesystem, corrupted lookups and reads. every operation
 * runs under one sleeping mutex (one disk for now; per-volume once there are
 * more), so other processes and interrupts keep running during disk I/O.
 */
static mutex_t ext2_lock = MUTEX_INIT;

static kerr_t locked_open(fs_mount_t *mount, const char *path, fs_file_t *file)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_open(mount, path, file);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_create(fs_mount_t *mount, const char *path)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_create(mount, path);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_mkdir(fs_mount_t *mount, const char *path)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_mkdir(mount, path);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_remove(fs_mount_t *mount, const char *path)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_remove(mount, path);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_rmdir(fs_mount_t *mount, const char *path)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_rmdir(mount, path);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_rename(fs_mount_t *mount, const char *old_path, const char *new_path)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_rename(mount, old_path, new_path);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_stat(fs_mount_t *mount, const char *path, fs_stat_t *stat_out)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_stat(mount, path, stat_out);
    mutex_unlock(&ext2_lock);
    return err;
}

static uint32_t locked_read(fs_file_t *file, uint32_t byte_count, void *data_out)
{
    mutex_lock(&ext2_lock);
    uint32_t n = ext2_ops_read(file, byte_count, data_out);
    mutex_unlock(&ext2_lock);
    return n;
}

static uint32_t locked_write(fs_file_t *file, uint32_t byte_count, const void *data_in)
{
    mutex_lock(&ext2_lock);
    uint32_t n = ext2_ops_write(file, byte_count, data_in);
    mutex_unlock(&ext2_lock);
    return n;
}

static kerr_t locked_truncate(fs_file_t *file)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_truncate(file);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_read_entry(fs_file_t *file, fs_dirent_t *entry_out)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_read_entry(file, entry_out);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_fstat(fs_file_t *file, fs_stat_t *stat_out)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_fstat(file, stat_out);
    mutex_unlock(&ext2_lock);
    return err;
}

/**
 * change an open file's permission bits and/or owner in its inode.
 */
static kerr_t ext2_ops_setattr(fs_file_t *file, const fs_attr_t *attr)
{
    ext2_file_t *ext2 = &file->ext2_file;
    ext2_inode_t inode;

    kerr_t err = ext2_read_inode(ext2->volume, ext2->inode, &inode);
    if (kerr_failed(err))
    {
        return err;
    }

    if (attr->what & FS_ATTR_MODE)
    {
        inode.i_mode = (uint16_t)((inode.i_mode & 0xF000u) | (attr->mode & 07777u));
    }
    if (attr->what & FS_ATTR_OWNER)
    {
        inode.i_uid = (uint16_t)attr->uid;
        inode.i_gid = (uint16_t)attr->gid;
    }
    inode.i_ctime = ext2_now();

    if (!ext2_write_inode(ext2->volume, ext2->inode, &inode))
    {
        return KERR_IO;
    }
    ext2->inode_cache = inode;
    return KERR_OK;
}

static kerr_t locked_setattr(fs_file_t *file, const fs_attr_t *attr)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_ops_setattr(file, attr);
    mutex_unlock(&ext2_lock);
    return err;
}

static void locked_close(fs_file_t *file)
{
    mutex_lock(&ext2_lock);
    ext2_ops_close(file);
    mutex_unlock(&ext2_lock);
}

static kerr_t locked_find_link(fs_mount_t *mount, const char *path, bool follow_last,
                               char *target, uint32_t size, uint32_t *end_out)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_find_link(&mount->ext2, path, follow_last, target, size, end_out);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_readlink(fs_mount_t *mount, const char *path, char *target, uint32_t size)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_readlink(&mount->ext2, path, target, size);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_symlink(fs_mount_t *mount, const char *path, const char *target)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_symlink(&mount->ext2, path, target);
    mutex_unlock(&ext2_lock);
    return err;
}

static kerr_t locked_link(fs_mount_t *mount, const char *old_path, const char *new_path)
{
    mutex_lock(&ext2_lock);
    kerr_t err = ext2_link(&mount->ext2, old_path, new_path);
    mutex_unlock(&ext2_lock);
    return err;
}

// the ext2 operations table handed to the generic fs layer at mount time.
static const fs_ops_t ext2_ops = {
    .open = locked_open,
    .create = locked_create,
    .mkdir = locked_mkdir,
    .remove = locked_remove,
    .rmdir = locked_rmdir,
    .rename = locked_rename,
    .stat = locked_stat,
    .read = locked_read,
    .write = locked_write,
    .truncate = locked_truncate,
    .read_entry = locked_read_entry,
    .fstat = locked_fstat,
    .setattr = locked_setattr,
    .close = locked_close,
    .find_link = locked_find_link,
    .readlink = locked_readlink,
    .symlink = locked_symlink,
    .link = locked_link,
};

kerr_t ext2_ops_mount(fs_mount_t *mount, block_device_t *device)
{
    kerr_t err = ext2_initialize(&mount->ext2, device);
    if (kerr_failed(err))
    {
        return err;
    }

    mount->ops = &ext2_ops;
    return KERR_OK;
}

#include "fs.h"
#include "ext2_ops.h"

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

    stat_out->inode = inode_number;
    stat_out->file_type = map_inode_type(inode.i_mode);
    stat_out->mode = inode.i_mode;
    stat_out->links_count = inode.i_links_count;
    stat_out->size = inode.i_size;
    stat_out->blocks = inode.i_blocks;
    stat_out->atime = inode.i_atime;
    stat_out->mtime = inode.i_mtime;
    stat_out->ctime = inode.i_ctime;
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

// the ext2 operations table handed to the generic fs layer at mount time.
static const fs_ops_t ext2_ops = {
    .open = ext2_ops_open,
    .create = ext2_ops_create,
    .mkdir = ext2_ops_mkdir,
    .remove = ext2_ops_remove,
    .rmdir = ext2_ops_rmdir,
    .rename = ext2_ops_rename,
    .stat = ext2_ops_stat,
    .read = ext2_ops_read,
    .write = ext2_ops_write,
    .truncate = ext2_ops_truncate,
    .read_entry = ext2_ops_read_entry,
    .close = ext2_ops_close,
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

#include "fs.h"
#include "common/memory.h"

/**
 * map EXT2 file type to FS file type.
 * @param ext2_type EXT2 file type value.
 * @return corresponding FS file type value.
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

bool fs_mount(fs_mount_t *mount, block_device_t *device)
{
    if (!mount || !device)
    {
        return false;
    }

    if (kerr_failed(ext2_initialize(&mount->ext2, device)))
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

    err = ext2_open(&mount->ext2, path, &file->ext2_file);
    if (kerr_failed(err))
    {
        return err;
    }

    file->file_type = map_ext2_file_type(file->ext2_file.file_type);
    file->is_open = 1;
    return KERR_OK;
}

kerr_t fs_create(fs_mount_t *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return ext2_create_file(&mount->ext2, path);
}

kerr_t fs_mkdir(fs_mount_t *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return ext2_create_dir(&mount->ext2, path);
}

kerr_t fs_remove(fs_mount_t *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return ext2_remove_file(&mount->ext2, path);
}

kerr_t fs_rmdir(fs_mount_t *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return ext2_remove_dir(&mount->ext2, path);
}

kerr_t fs_rename(fs_mount_t *mount, const char *old_path, const char *new_path)
{
    if (!mount || !old_path || !new_path || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    return ext2_rename(&mount->ext2, old_path, new_path);
}

kerr_t fs_stat(fs_mount_t *mount, const char *path, fs_stat_t *stat_out)
{
    uint32_t inode_number;
    ext2_inode_t inode;
    kerr_t err;

    if (!mount || !path || !stat_out || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

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

uint32_t fs_read(fs_file_t *file, uint32_t byteCount, void *dataOut)
{
    if (!file || !file->is_open)
    {
        return 0;
    }

    return ext2_read(&file->ext2_file, byteCount, dataOut);
}

uint32_t fs_write(fs_file_t *file, uint32_t byteCount, const void *dataIn)
{
    if (!file || !file->is_open)
    {
        return 0;
    }

    return ext2_write(&file->ext2_file, byteCount, dataIn);
}

kerr_t fs_truncate(fs_file_t *file)
{
    if (!file || !file->is_open)
    {
        return KERR_INVAL;
    }

    ext2_truncate(&file->ext2_file);
    return KERR_OK;
}

kerr_t fs_read_entry(fs_file_t *file, fs_dirent_t *entryOut)
{
    ext2_directory_entry_t ext2_entry;
    kerr_t err;

    if (!file || !entryOut || !file->is_open)
    {
        return KERR_INVAL;
    }

    err = ext2_read_entry(&file->ext2_file, &ext2_entry);
    if (kerr_failed(err))
    {
        return err;
    }

    entryOut->inode = ext2_entry.inode;
    entryOut->file_type = map_ext2_file_type(ext2_entry.file_type);
    entryOut->size = ext2_entry.size;
    memcpy(entryOut->name, ext2_entry.name, sizeof(entryOut->name));
    return KERR_OK;
}

void fs_close(fs_file_t *file)
{
    if (!file)
    {
        return;
    }

    ext2_close(&file->ext2_file);
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

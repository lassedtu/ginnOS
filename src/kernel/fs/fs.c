#include "fs.h"
#include "../../common/memory.h"

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

static FS_STATUS map_ext2_status(EXT2_STATUS status)
{
    switch (status)
    {
    case EXT2_OK:
        return FS_OK;
    case EXT2_NOT_FOUND:
        return FS_NOT_FOUND;
    case EXT2_PERMISSION_DENIED:
        return FS_PERMISSION_DENIED;
    default:
        return FS_IO_ERROR;
    }
}

bool fs_mount(FS_MOUNT *mount, BLOCK_DEVICE *device)
{
    if (!mount || !device)
    {
        return false;
    }

    if (!EXT2_Initialize(&mount->ext2, device))
    {
        return false;
    }

    mount->is_mounted = 1;
    return true;
}

bool fs_open(FS_MOUNT *mount, const char *path, FS_FILE *file)
{
    if (!mount || !file || !path || !mount->is_mounted)
    {
        return false;
    }

    if (!EXT2_Open(&mount->ext2, path, &file->ext2_file))
    {
        return false;
    }

    file->file_type = map_ext2_file_type(file->ext2_file.file_type);
    file->is_open = 1;
    return true;
}

bool fs_create(FS_MOUNT *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return false;
    }

    return EXT2_CreateFile(&mount->ext2, path);
}

bool fs_mkdir(FS_MOUNT *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return false;
    }

    return EXT2_CreateDir(&mount->ext2, path);
}

bool fs_remove(FS_MOUNT *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return false;
    }

    return EXT2_RemoveFile(&mount->ext2, path);
}

bool fs_rmdir(FS_MOUNT *mount, const char *path)
{
    if (!mount || !path || !mount->is_mounted)
    {
        return false;
    }

    return EXT2_RemoveDir(&mount->ext2, path);
}

bool fs_rename(FS_MOUNT *mount, const char *old_path, const char *new_path)
{
    if (!mount || !old_path || !new_path || !mount->is_mounted)
    {
        return false;
    }

    return EXT2_Rename(&mount->ext2, old_path, new_path);
}

FS_STATUS fs_stat(FS_MOUNT *mount, const char *path, FS_STAT *stat_out)
{
    uint32_t inode_number;
    EXT2_INODE inode;
    EXT2_STATUS lookup_status;

    if (!mount || !path || !stat_out || !mount->is_mounted)
    {
        return FS_IO_ERROR;
    }

    lookup_status = EXT2_LookupPath(&mount->ext2, path, &inode_number);
    if (lookup_status != EXT2_OK)
    {
        return map_ext2_status(lookup_status);
    }

    if (!EXT2_ReadInode(&mount->ext2, inode_number, &inode))
    {
        return FS_IO_ERROR;
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
    return FS_OK;
}

uint32_t fs_read(FS_FILE *file, uint32_t byteCount, void *dataOut)
{
    if (!file || !file->is_open)
    {
        return 0;
    }

    return EXT2_Read(&file->ext2_file, byteCount, dataOut);
}

uint32_t fs_write(FS_FILE *file, uint32_t byteCount, const void *dataIn)
{
    if (!file || !file->is_open)
    {
        return 0;
    }

    return EXT2_Write(&file->ext2_file, byteCount, dataIn);
}

bool fs_truncate(FS_FILE *file)
{
    if (!file || !file->is_open)
    {
        return false;
    }

    EXT2_Truncate(&file->ext2_file);
    return true;
}

bool fs_read_entry(FS_FILE *file, FS_DIRENT *entryOut)
{
    EXT2_DIRECTORY_ENTRY ext2_entry;

    if (!file || !entryOut || !file->is_open)
    {
        return false;
    }

    if (!EXT2_ReadEntry(&file->ext2_file, &ext2_entry))
    {
        return false;
    }

    entryOut->inode = ext2_entry.inode;
    entryOut->file_type = map_ext2_file_type(ext2_entry.file_type);
    entryOut->size = ext2_entry.size;
    memcpy(entryOut->name, ext2_entry.name, sizeof(entryOut->name));
    return true;
}

void fs_close(FS_FILE *file)
{
    if (!file)
    {
        return;
    }

    EXT2_Close(&file->ext2_file);
    file->is_open = 0;
    file->file_type = FS_TYPE_UNKNOWN;
}

uint8_t fs_file_type(const FS_FILE *file)
{
    if (!file)
    {
        return FS_TYPE_UNKNOWN;
    }

    return file->file_type;
}

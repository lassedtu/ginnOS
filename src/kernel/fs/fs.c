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

uint32_t fs_read(FS_FILE *file, uint32_t byteCount, void *dataOut)
{
    if (!file || !file->is_open)
    {
        return 0;
    }

    return EXT2_Read(&file->ext2_file, byteCount, dataOut);
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

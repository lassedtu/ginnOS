#include "ext2.h"
#include "../../common/memory.h"
#include "../../common/string.h"
#include "../../common/stdio.h"

#define EXT2_SECTOR_SIZE 512u
#define EXT2_MIN(a, b) ((a) < (b) ? (a) : (b))
#define OFFSETOF(type, member) ((uint32_t)&(((type *)0)->member))

static uint8_t g_sector_buffer[EXT2_SECTOR_SIZE];
static uint8_t g_block_buffer[EXT2_MAX_BLOCK_SIZE];
static uint8_t g_block_buffer2[EXT2_MAX_BLOCK_SIZE];
static uint8_t g_inode_buffer[EXT2_MAX_INODE_SIZE];

static bool read_abs_bytes(BLOCK_DEVICE *disk, uint32_t byte_offset, uint32_t size, void *out)
{
    uint8_t *dst = (uint8_t *)out;

    while (size > 0)
    {
        uint32_t lba = byte_offset / EXT2_SECTOR_SIZE;
        uint32_t offset = byte_offset % EXT2_SECTOR_SIZE;
        uint32_t chunk = EXT2_MIN(EXT2_SECTOR_SIZE - offset, size);

        if (!block_device_read(disk, lba, 1, g_sector_buffer))
        {
            return false;
        }

        memcpy(dst, g_sector_buffer + offset, chunk);

        dst += chunk;
        byte_offset += chunk;
        size -= chunk;
    }

    return true;
}

static bool read_block(EXT2_VOLUME *volume, uint32_t block, void *out)
{
    uint32_t lba;
    if (!volume || volume->sectors_per_block == 0)
    {
        return false;
    }

    lba = block * volume->sectors_per_block;
    return block_device_read(volume->disk, lba, (uint8_t)volume->sectors_per_block, out);
}

static bool read_group_desc(EXT2_VOLUME *volume, uint32_t group, EXT2_BLOCK_GROUP_DESC *out_desc)
{
    uint32_t bgdt_byte;
    uint32_t desc_offset;

    if (!volume || !out_desc)
    {
        return false;
    }

    bgdt_byte = volume->bgdt_start_block * volume->block_size;
    desc_offset = group * (uint32_t)sizeof(EXT2_BLOCK_GROUP_DESC);
    return read_abs_bytes(volume->disk, bgdt_byte + desc_offset, (uint32_t)sizeof(EXT2_BLOCK_GROUP_DESC), out_desc);
}

static bool inode_is_dir(const EXT2_INODE *inode)
{
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

static bool inode_is_regular(const EXT2_INODE *inode)
{
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG;
}

static bool inode_is_directory(const EXT2_INODE *inode)
{
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

static uint8_t inode_to_file_type(const EXT2_INODE *inode)
{
    if (inode_is_directory(inode))
    {
        return EXT2_FT_DIR;
    }

    if (inode_is_regular(inode))
    {
        return EXT2_FT_REG_FILE;
    }

    return 0;
}

static bool find_in_directory(EXT2_VOLUME *volume, uint32_t dir_inode_number, const char *name, uint32_t name_len, uint32_t *inode_out)
{
    EXT2_INODE dir_inode;
    uint32_t block_index;
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;

    if (!inode_out)
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, dir_inode_number, &dir_inode) || !inode_is_dir(&dir_inode))
    {
        return false;
    }

    for (block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        uint32_t data_block = dir_inode.i_block[block_index];
        uint32_t offset = 0;

        if (data_block == 0)
        {
            continue;
        }

        if (!read_block(volume, data_block, g_block_buffer))
        {
            return false;
        }

        while (offset + header_size <= volume->block_size)
        {
            EXT2_DIR_ENTRY *entry = (EXT2_DIR_ENTRY *)(g_block_buffer + offset);
            uint32_t min_size = header_size + entry->name_len;
            const char *entry_name;

            if (entry->rec_len < min_size || entry->rec_len == 0 || offset + entry->rec_len > volume->block_size)
            {
                return false;
            }

            entry_name = (const char *)(g_block_buffer + offset + header_size);
            if (entry->inode != 0 && name_len == entry->name_len && memcmp(name, entry_name, name_len) == 0)
            {
                *inode_out = entry->inode;
                return true;
            }

            offset += entry->rec_len;
        }
    }

    return false;
}

static bool resolve_data_block(EXT2_VOLUME *volume, const EXT2_INODE *inode, uint32_t logical_block_index, uint32_t *physical_block_out)
{
    if (logical_block_index < EXT2_NDIR_BLOCKS)
    {
        *physical_block_out = inode->i_block[logical_block_index];
        return *physical_block_out != 0;
    }

    logical_block_index -= EXT2_NDIR_BLOCKS;

    if (logical_block_index < (volume->block_size / 4u))
    {
        uint32_t *entries;
        uint32_t indirect = inode->i_block[12];

        if (indirect == 0)
        {
            return false;
        }

        if (!read_block(volume, indirect, g_block_buffer2))
        {
            return false;
        }

        entries = (uint32_t *)g_block_buffer2;
        *physical_block_out = entries[logical_block_index];
        return *physical_block_out != 0;
    }

    return false;
}

bool EXT2_Initialize(EXT2_VOLUME *volume, BLOCK_DEVICE *disk)
{
    EXT2_SUPERBLOCK sb;
    uint32_t unsupported;

    if (!volume || !disk)
    {
        return false;
    }

    if (!read_abs_bytes(disk, EXT2_SUPERBLOCK_OFFSET, (uint32_t)sizeof(EXT2_SUPERBLOCK), &sb))
    {
        return false;
    }

    if (sb.s_magic != EXT2_SUPERBLOCK_MAGIC)
    {
        return false;
    }

    volume->disk = disk;
    volume->block_size = 1024u << sb.s_log_block_size;
    volume->inode_size = (sb.s_inode_size == 0) ? 128u : sb.s_inode_size;
    volume->first_data_block = sb.s_first_data_block;
    volume->blocks_per_group = sb.s_blocks_per_group;
    volume->inodes_per_group = sb.s_inodes_per_group;
    volume->bgdt_start_block = sb.s_first_data_block + 1u;

    if (volume->block_size < 1024u || volume->block_size > EXT2_MAX_BLOCK_SIZE)
    {
        return false;
    }

    if ((volume->block_size % EXT2_SECTOR_SIZE) != 0)
    {
        return false;
    }

    volume->sectors_per_block = volume->block_size / EXT2_SECTOR_SIZE;

    if (volume->inode_size > EXT2_MAX_INODE_SIZE || volume->inode_size < 128u)
    {
        return false;
    }

    if (volume->blocks_per_group == 0 || volume->inodes_per_group == 0)
    {
        return false;
    }

    unsupported = sb.s_feature_incompat & ~(EXT2_FEATURE_INCOMPAT_FILETYPE);
    if (unsupported != 0)
    {
        return false;
    }

    volume->block_group_count = (sb.s_blocks_count - sb.s_first_data_block + sb.s_blocks_per_group - 1u) / sb.s_blocks_per_group;
    if (volume->block_group_count == 0)
    {
        return false;
    }

    return true;
}

bool EXT2_ReadInode(EXT2_VOLUME *volume, uint32_t inode_number, EXT2_INODE *inode_out)
{
    EXT2_BLOCK_GROUP_DESC bgd;
    uint32_t zero_based;
    uint32_t group;
    uint32_t index;
    uint32_t inode_byte_offset;

    if (!volume || !inode_out || inode_number == 0)
    {
        return false;
    }

    zero_based = inode_number - 1u;
    group = zero_based / volume->inodes_per_group;
    index = zero_based % volume->inodes_per_group;

    if (group >= volume->block_group_count)
    {
        return false;
    }

    if (!read_group_desc(volume, group, &bgd))
    {
        return false;
    }

    inode_byte_offset = (bgd.bg_inode_table * volume->block_size) + (index * volume->inode_size);
    if (!read_abs_bytes(volume->disk, inode_byte_offset, volume->inode_size, g_inode_buffer))
    {
        return false;
    }

    memcpy(inode_out, g_inode_buffer, (uint32_t)sizeof(EXT2_INODE));
    return true;
}

bool EXT2_ListDirectory(EXT2_VOLUME *volume, uint32_t inode_number)
{
    EXT2_INODE dir_inode;
    uint32_t block_index;
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;

    if (!volume)
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, inode_number, &dir_inode) || !inode_is_directory(&dir_inode))
    {
        return false;
    }

    for (block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        uint32_t data_block = dir_inode.i_block[block_index];
        uint32_t offset = 0;

        if (data_block == 0)
        {
            continue;
        }

        if (!read_block(volume, data_block, g_block_buffer))
        {
            return false;
        }

        while (offset + header_size <= volume->block_size)
        {
            EXT2_DIR_ENTRY *entry = (EXT2_DIR_ENTRY *)(g_block_buffer + offset);
            uint32_t min_size = header_size + entry->name_len;

            if (entry->rec_len < min_size || entry->rec_len == 0 || offset + entry->rec_len > volume->block_size)
            {
                return false;
            }

            if (entry->inode != 0 && entry->name_len > 0 && entry->name_len < 240)
            {
                char name[240];
                const char *src = (const char *)(g_block_buffer + offset + header_size);

                memcpy(name, src, entry->name_len);
                name[entry->name_len] = 0;
                printf("  %s\r\n", name);
            }

            offset += entry->rec_len;
        }
    }

    return true;
}

bool EXT2_LookupPath(EXT2_VOLUME *volume, const char *path, uint32_t *inode_out)
{
    uint32_t current = EXT2_INODE_ROOT;
    uint32_t i = 0;

    if (!volume || !path || !inode_out)
    {
        return false;
    }

    if (path[0] == 0)
    {
        return false;
    }

    while (path[i] == '/')
    {
        i++;
    }

    if (path[i] == 0)
    {
        *inode_out = EXT2_INODE_ROOT;
        return true;
    }

    while (path[i] != 0)
    {
        uint32_t start = i;
        uint32_t len;
        uint32_t next_inode;

        while (path[i] != 0 && path[i] != '/')
        {
            i++;
        }

        len = i - start;
        if (len == 0)
        {
            while (path[i] == '/')
            {
                i++;
            }
            continue;
        }

        if (!find_in_directory(volume, current, &path[start], len, &next_inode))
        {
            return false;
        }

        current = next_inode;

        while (path[i] == '/')
        {
            i++;
        }
    }

    *inode_out = current;
    return true;
}

static bool read_directory_entry(EXT2_FILE *file, EXT2_DIRECTORY_ENTRY *entryOut)
{
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;
    uint32_t block_size;

    if (!file || !entryOut || !file->volume || !file->is_open || !inode_is_directory(&file->inode_cache))
    {
        return false;
    }

    block_size = file->volume->block_size;

    while (file->cursor < file->inode_cache.i_size)
    {
        uint32_t block_index = file->cursor / block_size;
        uint32_t block_offset = file->cursor % block_size;
        uint32_t physical_block;
        EXT2_DIR_ENTRY *entry;

        if (!resolve_data_block(file->volume, &file->inode_cache, block_index, &physical_block))
        {
            return false;
        }

        if (!read_block(file->volume, physical_block, g_block_buffer))
        {
            return false;
        }

        if (block_offset + header_size > block_size)
        {
            file->cursor = (block_index + 1u) * block_size;
            continue;
        }

        entry = (EXT2_DIR_ENTRY *)(g_block_buffer + block_offset);
        if (entry->rec_len == 0 || block_offset + entry->rec_len > block_size)
        {
            return false;
        }

        file->cursor += entry->rec_len;

        entryOut->inode = entry->inode;
        entryOut->file_type = entry->file_type;
        entryOut->size = 0;

        memcpy(entryOut->name, g_block_buffer + block_offset + header_size, entry->name_len);
        entryOut->name[entry->name_len] = 0;
        return true;
    }

    return false;
}

bool EXT2_Open(EXT2_VOLUME *volume, const char *path, EXT2_FILE *file)
{
    uint32_t inode_number;
    EXT2_INODE inode;

    if (!volume || !path || !file)
    {
        return false;
    }

    if (!EXT2_LookupPath(volume, path, &inode_number))
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, inode_number, &inode))
    {
        return false;
    }

    file->inode = inode_number;
    file->size = inode.i_size;
    file->cursor = 0;
    file->file_type = inode_to_file_type(&inode);
    file->is_open = 1;
    file->volume = volume;
    file->inode_cache = inode;
    return true;
}

uint32_t EXT2_Read(EXT2_FILE *file, uint32_t byteCount, void *dataOut)
{
    uint32_t available;

    if (!file || !file->is_open || !file->volume || !dataOut)
    {
        return 0;
    }

    if (!inode_is_regular(&file->inode_cache))
    {
        return 0;
    }

    if (file->cursor >= file->size)
    {
        return 0;
    }

    available = file->size - file->cursor;
    if (byteCount > available)
    {
        byteCount = available;
    }

    if (byteCount == 0)
    {
        return 0;
    }

    if (!EXT2_ReadFile(file->volume, file->inode, file->cursor, byteCount, dataOut))
    {
        return 0;
    }

    file->cursor += byteCount;
    return byteCount;
}

bool EXT2_ReadEntry(EXT2_FILE *file, EXT2_DIRECTORY_ENTRY *entryOut)
{
    if (!file || !entryOut)
    {
        return false;
    }

    if (!file->is_open)
    {
        return false;
    }

    if (!inode_is_directory(&file->inode_cache))
    {
        return false;
    }

    return read_directory_entry(file, entryOut);
}

void EXT2_Close(EXT2_FILE *file)
{
    if (!file)
    {
        return;
    }

    file->inode = 0;
    file->size = 0;
    file->cursor = 0;
    file->file_type = 0;
    file->is_open = 0;
    file->volume = 0;
    memset(&file->inode_cache, 0, (uint32_t)sizeof(EXT2_INODE));
}

bool EXT2_ReadFile(EXT2_VOLUME *volume, uint32_t inode_number, uint32_t offset, uint32_t length, void *buffer)
{
    EXT2_INODE inode;
    uint8_t *out = (uint8_t *)buffer;
    uint32_t copied = 0;

    if (!volume || !buffer)
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, inode_number, &inode) || !inode_is_regular(&inode))
    {
        return false;
    }

    if (length == 0)
    {
        return true;
    }

    if (offset >= inode.i_size)
    {
        return false;
    }

    if (offset + length < offset)
    {
        return false;
    }

    if (offset + length > inode.i_size)
    {
        length = inode.i_size - offset;
    }

    while (copied < length)
    {
        uint32_t file_pos = offset + copied;
        uint32_t block_index = file_pos / volume->block_size;
        uint32_t block_offset = file_pos % volume->block_size;
        uint32_t take = EXT2_MIN(length - copied, volume->block_size - block_offset);
        uint32_t phys_block;
        uint32_t j;

        if (!resolve_data_block(volume, &inode, block_index, &phys_block))
        {
            return false;
        }

        if (!read_block(volume, phys_block, g_block_buffer))
        {
            return false;
        }

        for (j = 0; j < take; j++)
        {
            out[copied + j] = g_block_buffer[block_offset + j];
        }

        copied += take;
    }

    return true;
}

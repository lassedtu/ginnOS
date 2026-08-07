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
static uint8_t g_bitmap_buffer[EXT2_MAX_BLOCK_SIZE];

static bool inode_is_dir(const EXT2_INODE *inode);
static bool inode_is_regular(const EXT2_INODE *inode);
static bool inode_is_directory(const EXT2_INODE *inode);

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

static uint32_t ext2_align4(uint32_t value)
{
    return (value + 3u) & ~3u;
}

static uint32_t ext2_dir_entry_size(uint32_t name_len)
{
    return ext2_align4(OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1u + name_len);
}

static bool write_abs_bytes(BLOCK_DEVICE *disk, uint32_t byte_offset, uint32_t size, const void *in)
{
    const uint8_t *src = (const uint8_t *)in;

    if (!disk || !src)
    {
        return false;
    }

    while (size > 0)
    {
        uint32_t lba = byte_offset / EXT2_SECTOR_SIZE;
        uint32_t offset = byte_offset % EXT2_SECTOR_SIZE;
        uint32_t chunk = EXT2_MIN(EXT2_SECTOR_SIZE - offset, size);

        if (offset == 0 && chunk == EXT2_SECTOR_SIZE)
        {
            if (!block_device_write(disk, lba, 1, src))
            {
                return false;
            }
        }
        else
        {
            if (!block_device_read(disk, lba, 1, g_sector_buffer))
            {
                return false;
            }

            memcpy(g_sector_buffer + offset, src, chunk);

            if (!block_device_write(disk, lba, 1, g_sector_buffer))
            {
                return false;
            }
        }

        src += chunk;
        byte_offset += chunk;
        size -= chunk;
    }

    return true;
}

static bool write_block(EXT2_VOLUME *volume, uint32_t block, const void *in)
{
    if (!volume || !volume->disk || !in || volume->sectors_per_block == 0)
    {
        return false;
    }

    return block_device_write(volume->disk, block * volume->sectors_per_block, (uint8_t)volume->sectors_per_block, in);
}

static bool write_group_desc(EXT2_VOLUME *volume, uint32_t group, const EXT2_BLOCK_GROUP_DESC *desc)
{
    uint32_t bgdt_byte;
    uint32_t desc_offset;

    if (!volume || !desc || group >= volume->block_group_count)
    {
        return false;
    }

    bgdt_byte = volume->bgdt_start_block * volume->block_size;
    desc_offset = group * (uint32_t)sizeof(EXT2_BLOCK_GROUP_DESC);
    return write_abs_bytes(volume->disk, bgdt_byte + desc_offset, (uint32_t)sizeof(EXT2_BLOCK_GROUP_DESC), desc);
}

static bool write_superblock(EXT2_VOLUME *volume)
{
    if (!volume)
    {
        return false;
    }

    return write_abs_bytes(volume->disk, EXT2_SUPERBLOCK_OFFSET, (uint32_t)sizeof(EXT2_SUPERBLOCK), &volume->superblock);
}

static bool read_inode_bitmap(EXT2_VOLUME *volume, uint32_t group, uint8_t *bitmap)
{
    EXT2_BLOCK_GROUP_DESC desc;

    if (!volume || !bitmap || group >= volume->block_group_count)
    {
        return false;
    }

    if (!read_group_desc(volume, group, &desc))
    {
        return false;
    }

    return read_block(volume, desc.bg_inode_bitmap, bitmap);
}

static bool write_inode_bitmap(EXT2_VOLUME *volume, uint32_t group, const uint8_t *bitmap)
{
    EXT2_BLOCK_GROUP_DESC desc;

    if (!volume || !bitmap || group >= volume->block_group_count)
    {
        return false;
    }

    if (!read_group_desc(volume, group, &desc))
    {
        return false;
    }

    return write_block(volume, desc.bg_inode_bitmap, bitmap);
}

static bool read_block_bitmap(EXT2_VOLUME *volume, uint32_t group, uint8_t *bitmap)
{
    EXT2_BLOCK_GROUP_DESC desc;

    if (!volume || !bitmap || group >= volume->block_group_count)
    {
        return false;
    }

    if (!read_group_desc(volume, group, &desc))
    {
        return false;
    }

    return read_block(volume, desc.bg_block_bitmap, bitmap);
}

static bool write_block_bitmap(EXT2_VOLUME *volume, uint32_t group, const uint8_t *bitmap)
{
    EXT2_BLOCK_GROUP_DESC desc;

    if (!volume || !bitmap || group >= volume->block_group_count)
    {
        return false;
    }

    if (!read_group_desc(volume, group, &desc))
    {
        return false;
    }

    return write_block(volume, desc.bg_block_bitmap, bitmap);
}

static bool bitmap_test(const uint8_t *bitmap, uint32_t index)
{
    return (bitmap[index / 8u] & (uint8_t)(1u << (index % 8u))) != 0;
}

static void bitmap_set(uint8_t *bitmap, uint32_t index, bool value)
{
    uint8_t mask = (uint8_t)(1u << (index % 8u));

    if (value)
    {
        bitmap[index / 8u] |= mask;
    }
    else
    {
        bitmap[index / 8u] &= (uint8_t)~mask;
    }
}

static bool bitmap_find_free(const uint8_t *bitmap, uint32_t bit_count, uint32_t start_bit, uint32_t *bit_out)
{
    uint32_t i;

    if (!bitmap || !bit_out)
    {
        return false;
    }

    for (i = start_bit; i < bit_count; i++)
    {
        if (!bitmap_test(bitmap, i))
        {
            *bit_out = i;
            return true;
        }
    }

    return false;
}

static bool inode_location(EXT2_VOLUME *volume, uint32_t inode_number, EXT2_BLOCK_GROUP_DESC *desc_out, uint32_t *inode_byte_offset_out, uint32_t *group_out)
{
    uint32_t zero_based;
    uint32_t group;
    uint32_t index;
    EXT2_BLOCK_GROUP_DESC desc;

    if (!volume || !inode_number || !desc_out || !inode_byte_offset_out)
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

    if (!read_group_desc(volume, group, &desc))
    {
        return false;
    }

    *desc_out = desc;
    *inode_byte_offset_out = (desc.bg_inode_table * volume->block_size) + (index * volume->inode_size);

    if (group_out)
    {
        *group_out = group;
    }

    return true;
}

static bool write_inode(EXT2_VOLUME *volume, uint32_t inode_number, const EXT2_INODE *inode)
{
    EXT2_BLOCK_GROUP_DESC desc;
    uint32_t inode_byte_offset;
    uint8_t inode_buffer[EXT2_MAX_INODE_SIZE];

    if (!volume || !inode || volume->inode_size > EXT2_MAX_INODE_SIZE)
    {
        return false;
    }

    if (!inode_location(volume, inode_number, &desc, &inode_byte_offset, 0))
    {
        return false;
    }

    memset(inode_buffer, 0, sizeof(inode_buffer));
    memcpy(inode_buffer, inode, sizeof(EXT2_INODE));
    return write_abs_bytes(volume->disk, inode_byte_offset, volume->inode_size, inode_buffer);
}

static bool update_group_and_super_counts(EXT2_VOLUME *volume, uint32_t group, int32_t free_blocks_delta, int32_t free_inodes_delta, int32_t used_dirs_delta)
{
    EXT2_BLOCK_GROUP_DESC desc;
    int32_t new_free_blocks;
    int32_t new_free_inodes;
    int32_t new_used_dirs;

    if (!volume || group >= volume->block_group_count)
    {
        return false;
    }

    if (!read_group_desc(volume, group, &desc))
    {
        return false;
    }

    if (free_blocks_delta < 0 && desc.bg_free_blocks_count < (uint16_t)(-free_blocks_delta))
    {
        return false;
    }

    if (free_inodes_delta < 0 && desc.bg_free_inodes_count < (uint16_t)(-free_inodes_delta))
    {
        return false;
    }

    if (used_dirs_delta < 0 && desc.bg_used_dirs_count < (uint16_t)(-used_dirs_delta))
    {
        return false;
    }

    new_free_blocks = (int32_t)desc.bg_free_blocks_count + free_blocks_delta;
    new_free_inodes = (int32_t)desc.bg_free_inodes_count + free_inodes_delta;
    new_used_dirs = (int32_t)desc.bg_used_dirs_count + used_dirs_delta;

    if (new_free_blocks < 0 || new_free_inodes < 0 || new_used_dirs < 0)
    {
        return false;
    }

    if ((int32_t)volume->superblock.s_free_blocks_count + free_blocks_delta < 0 || (int32_t)volume->superblock.s_free_inodes_count + free_inodes_delta < 0)
    {
        return false;
    }

    desc.bg_free_blocks_count = (uint16_t)new_free_blocks;
    desc.bg_free_inodes_count = (uint16_t)new_free_inodes;
    desc.bg_used_dirs_count = (uint16_t)new_used_dirs;

    volume->superblock.s_free_blocks_count = (uint32_t)((int32_t)volume->superblock.s_free_blocks_count + free_blocks_delta);
    volume->superblock.s_free_inodes_count = (uint32_t)((int32_t)volume->superblock.s_free_inodes_count + free_inodes_delta);

    if (!write_group_desc(volume, group, &desc))
    {
        return false;
    }

    return write_superblock(volume);
}

static bool alloc_inode(EXT2_VOLUME *volume, uint32_t *inode_number_out)
{
    uint32_t group;

    if (!volume || !inode_number_out)
    {
        return false;
    }

    for (group = 0; group < volume->block_group_count; group++)
    {
        EXT2_BLOCK_GROUP_DESC desc;
        uint32_t bit_limit;
        uint32_t start_bit = 0;
        uint32_t free_bit;

        if (!read_group_desc(volume, group, &desc))
        {
            return false;
        }

        if (desc.bg_free_inodes_count == 0)
        {
            continue;
        }

        if (group == 0 && volume->first_non_reserved_inode > 0)
        {
            start_bit = volume->first_non_reserved_inode - 1u;
        }

        bit_limit = volume->inodes_per_group;
        if (group == volume->block_group_count - 1u)
        {
            uint32_t remaining = volume->inode_count - (group * volume->inodes_per_group);
            if (remaining < bit_limit)
            {
                bit_limit = remaining;
            }
        }

        if (!read_inode_bitmap(volume, group, g_bitmap_buffer))
        {
            return false;
        }

        if (!bitmap_find_free(g_bitmap_buffer, bit_limit, start_bit, &free_bit))
        {
            continue;
        }

        bitmap_set(g_bitmap_buffer, free_bit, true);
        if (!write_inode_bitmap(volume, group, g_bitmap_buffer))
        {
            return false;
        }

        if (!update_group_and_super_counts(volume, group, 0, -1, 0))
        {
            return false;
        }

        *inode_number_out = (group * volume->inodes_per_group) + free_bit + 1u;
        return true;
    }

    return false;
}

static bool free_inode(EXT2_VOLUME *volume, uint32_t inode_number)
{
    EXT2_BLOCK_GROUP_DESC desc;
    uint32_t zero_based;
    uint32_t group;
    uint32_t index;

    if (!volume || inode_number == 0)
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

    if (!read_group_desc(volume, group, &desc))
    {
        return false;
    }

    if (!read_inode_bitmap(volume, group, g_bitmap_buffer))
    {
        return false;
    }

    bitmap_set(g_bitmap_buffer, index, false);
    if (!write_inode_bitmap(volume, group, g_bitmap_buffer))
    {
        return false;
    }

    return update_group_and_super_counts(volume, group, 0, 1, 0);
}

static bool alloc_block(EXT2_VOLUME *volume, uint32_t *block_number_out)
{
    uint32_t group;

    if (!volume || !block_number_out)
    {
        return false;
    }

    for (group = 0; group < volume->block_group_count; group++)
    {
        EXT2_BLOCK_GROUP_DESC desc;
        uint32_t bit_limit;
        uint32_t start_block;
        uint32_t free_bit;

        if (!read_group_desc(volume, group, &desc))
        {
            return false;
        }

        if (desc.bg_free_blocks_count == 0)
        {
            continue;
        }

        if (!read_block_bitmap(volume, group, g_bitmap_buffer))
        {
            return false;
        }

        start_block = volume->first_data_block + (group * volume->blocks_per_group);
        if (start_block >= volume->block_count)
        {
            continue;
        }

        bit_limit = volume->blocks_per_group;
        if (start_block + bit_limit > volume->block_count)
        {
            bit_limit = volume->block_count - start_block;
        }

        if (!bitmap_find_free(g_bitmap_buffer, bit_limit, 0, &free_bit))
        {
            continue;
        }

        bitmap_set(g_bitmap_buffer, free_bit, true);
        if (!write_block_bitmap(volume, group, g_bitmap_buffer))
        {
            return false;
        }

        if (!update_group_and_super_counts(volume, group, -1, 0, 0))
        {
            return false;
        }

        *block_number_out = start_block + free_bit;
        return true;
    }

    return false;
}

static bool free_block(EXT2_VOLUME *volume, uint32_t block_number)
{
    EXT2_BLOCK_GROUP_DESC desc;
    uint32_t relative;
    uint32_t group;

    if (!volume)
    {
        return false;
    }

    if (block_number < volume->first_data_block)
    {
        return false;
    }

    relative = block_number - volume->first_data_block;
    group = relative / volume->blocks_per_group;
    if (group >= volume->block_group_count)
    {
        return false;
    }

    if (!read_group_desc(volume, group, &desc))
    {
        return false;
    }

    if (!read_block_bitmap(volume, group, g_bitmap_buffer))
    {
        return false;
    }

    bitmap_set(g_bitmap_buffer, relative % volume->blocks_per_group, false);
    if (!write_block_bitmap(volume, group, g_bitmap_buffer))
    {
        return false;
    }

    return update_group_and_super_counts(volume, group, 1, 0, 0);
}

static bool free_inode_block_chain(EXT2_VOLUME *volume, EXT2_INODE *inode)
{
    uint32_t i;

    if (!volume || !inode)
    {
        return false;
    }

    for (i = 0; i < EXT2_NDIR_BLOCKS; i++)
    {
        if (inode->i_block[i] != 0)
        {
            if (!free_block(volume, inode->i_block[i]))
            {
                return false;
            }

            inode->i_block[i] = 0;
        }
    }

    if (inode->i_block[12] != 0)
    {
        uint32_t *entries;
        uint32_t entry_count;

        if (!read_block(volume, inode->i_block[12], g_block_buffer))
        {
            return false;
        }

        entries = (uint32_t *)g_block_buffer;
        entry_count = volume->block_size / 4u;
        for (i = 0; i < entry_count; i++)
        {
            if (entries[i] != 0)
            {
                if (!free_block(volume, entries[i]))
                {
                    return false;
                }
            }
        }

        if (!free_block(volume, inode->i_block[12]))
        {
            return false;
        }

        inode->i_block[12] = 0;
    }

    return true;
}

static bool split_path(const char *path, char *parent, uint32_t parent_size, char *name, uint32_t name_size)
{
    uint32_t len;
    uint32_t end;
    uint32_t start;
    uint32_t parent_len;
    uint32_t i;

    if (!path || !parent || !name || parent_size == 0 || name_size == 0 || path[0] != '/')
    {
        return false;
    }

    len = (uint32_t)strlen(path);
    while (len > 1u && path[len - 1u] == '/')
    {
        len--;
    }

    if (len <= 1u)
    {
        return false;
    }

    end = len;
    start = end;
    while (start > 0u && path[start - 1u] != '/')
    {
        start--;
    }

    if (start == 0u)
    {
        return false;
    }

    if ((end - start) + 1u > name_size)
    {
        return false;
    }

    for (i = 0; i < end - start; i++)
    {
        name[i] = path[start + i];
    }
    name[end - start] = '\0';

    parent_len = start - 1u;
    if (parent_len == 0u)
    {
        if (parent_size < 2u)
        {
            return false;
        }

        parent[0] = '/';
        parent[1] = '\0';
        return true;
    }

    if (parent_len + 1u > parent_size)
    {
        return false;
    }

    for (i = 0; i < parent_len; i++)
    {
        parent[i] = path[i];
    }
    parent[parent_len] = '\0';
    return true;
}

static bool find_directory_entry(EXT2_VOLUME *volume, uint32_t dir_inode_number, const char *name, uint32_t name_len, uint32_t *block_index_out, uint32_t *offset_out, EXT2_DIR_ENTRY *entry_out)
{
    EXT2_INODE dir_inode;
    uint32_t block_index;
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;

    if (!volume || !name || !block_index_out || !offset_out || !entry_out)
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
                *block_index_out = block_index;
                *offset_out = offset;
                *entry_out = *entry;
                return true;
            }

            offset += entry->rec_len;
        }
    }

    return false;
}

static bool append_or_replace_directory_entry(EXT2_VOLUME *volume, uint32_t parent_inode_number, EXT2_INODE *parent_inode, const char *name, uint32_t name_len, uint32_t child_inode_number, uint8_t file_type)
{
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;
    uint32_t needed = ext2_dir_entry_size(name_len);
    uint32_t block_index;
    if (!volume || !parent_inode || !name || name_len == 0 || name_len >= 255)
    {
        return false;
    }

    for (block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        uint32_t data_block = parent_inode->i_block[block_index];
        uint32_t cursor = 0;

        if (data_block == 0)
        {
            continue;
        }

        if (!read_block(volume, data_block, g_block_buffer))
        {
            return false;
        }

        while (cursor + header_size <= volume->block_size)
        {
            EXT2_DIR_ENTRY *entry = (EXT2_DIR_ENTRY *)(g_block_buffer + cursor);
            uint32_t used = ext2_dir_entry_size(entry->name_len);
            uint32_t available;

            if (entry->rec_len == 0 || cursor + entry->rec_len > volume->block_size)
            {
                return false;
            }

            available = entry->rec_len;
            if (entry->inode == 0)
            {
                if (available >= needed)
                {
                    EXT2_DIR_ENTRY *new_entry = entry;

                    memset(new_entry, 0, available);
                    new_entry->inode = child_inode_number;
                    new_entry->rec_len = (uint16_t)available;
                    new_entry->name_len = (uint8_t)name_len;
                    new_entry->file_type = file_type;
                    memcpy((uint8_t *)new_entry + header_size, name, name_len);
                    return write_block(volume, data_block, g_block_buffer);
                }
            }
            else if (available >= used + needed)
            {
                EXT2_DIR_ENTRY *new_entry = (EXT2_DIR_ENTRY *)(g_block_buffer + cursor + used);
                uint32_t remaining = available - used;

                entry->rec_len = (uint16_t)used;
                memset(new_entry, 0, remaining);
                new_entry->inode = child_inode_number;
                new_entry->rec_len = (uint16_t)remaining;
                new_entry->name_len = (uint8_t)name_len;
                new_entry->file_type = file_type;
                memcpy((uint8_t *)new_entry + header_size, name, name_len);
                return write_block(volume, data_block, g_block_buffer);
            }

            cursor += entry->rec_len;
        }
    }

    for (block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        if (parent_inode->i_block[block_index] == 0)
        {
            uint32_t new_block;
            EXT2_DIR_ENTRY *entry;

            if (!alloc_block(volume, &new_block))
            {
                return false;
            }

            memset(g_block_buffer, 0, volume->block_size);
            entry = (EXT2_DIR_ENTRY *)g_block_buffer;
            entry->inode = child_inode_number;
            entry->rec_len = (uint16_t)volume->block_size;
            entry->name_len = (uint8_t)name_len;
            entry->file_type = file_type;
            memcpy((uint8_t *)entry + header_size, name, name_len);

            parent_inode->i_block[block_index] = new_block;
            parent_inode->i_size += volume->block_size;
            parent_inode->i_blocks += volume->sectors_per_block;
            return write_block(volume, new_block, g_block_buffer) && write_inode(volume, parent_inode_number, parent_inode);
        }
    }

    return false;
}

static bool update_directory_entry_name(EXT2_VOLUME *volume, uint32_t dir_inode_number, const char *old_name, uint32_t old_name_len, const char *new_name, uint32_t new_name_len)
{
    EXT2_INODE dir_inode;
    uint32_t block_index;
    uint32_t entry_offset;
    EXT2_DIR_ENTRY entry;
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;
    uint32_t needed = ext2_dir_entry_size(new_name_len);

    if (!volume || !old_name || !new_name || new_name_len == 0 || new_name_len >= 255)
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, dir_inode_number, &dir_inode) || !inode_is_dir(&dir_inode))
    {
        return false;
    }

    if (!find_directory_entry(volume, dir_inode_number, old_name, old_name_len, &block_index, &entry_offset, &entry))
    {
        return false;
    }

    if (!read_block(volume, dir_inode.i_block[block_index], g_block_buffer))
    {
        return false;
    }

    if (needed > entry.rec_len)
    {
        return false;
    }

    ((EXT2_DIR_ENTRY *)(g_block_buffer + entry_offset))->name_len = (uint8_t)new_name_len;
    ((EXT2_DIR_ENTRY *)(g_block_buffer + entry_offset))->file_type = entry.file_type;
    memcpy(g_block_buffer + entry_offset + header_size, new_name, new_name_len);
    return write_block(volume, dir_inode.i_block[block_index], g_block_buffer);
}

static bool directory_is_empty(EXT2_VOLUME *volume, uint32_t inode_number)
{
    EXT2_INODE dir_inode;
    uint32_t block_index;
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;

    if (!EXT2_ReadInode(volume, inode_number, &dir_inode) || !inode_is_dir(&dir_inode))
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

            if (entry->inode != 0)
            {
                const char *entry_name = (const char *)(g_block_buffer + offset + header_size);

                if (!(entry->name_len == 1u && entry_name[0] == '.') && !(entry->name_len == 2u && entry_name[0] == '.' && entry_name[1] == '.'))
                {
                    return false;
                }
            }

            offset += entry->rec_len;
        }
    }

    return true;
}

static bool update_directory_parent_link(EXT2_VOLUME *volume, uint32_t dir_inode_number, uint32_t parent_inode_number)
{
    EXT2_INODE dir_inode;
    EXT2_DIR_ENTRY *entry;
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;

    if (!EXT2_ReadInode(volume, dir_inode_number, &dir_inode) || !inode_is_dir(&dir_inode))
    {
        return false;
    }

    if (dir_inode.i_block[0] == 0)
    {
        return false;
    }

    if (!read_block(volume, dir_inode.i_block[0], g_block_buffer))
    {
        return false;
    }

    entry = (EXT2_DIR_ENTRY *)(g_block_buffer + ext2_dir_entry_size(1u));
    if (entry->name_len != 2u)
    {
        return false;
    }

    if (memcmp((uint8_t *)entry + header_size, "..", 2u) != 0)
    {
        return false;
    }

    entry->inode = parent_inode_number;
    return write_block(volume, dir_inode.i_block[0], g_block_buffer);
}

static bool remove_directory_entry(EXT2_VOLUME *volume, uint32_t dir_inode_number, const char *name, uint32_t name_len)
{
    EXT2_INODE dir_inode;
    uint32_t block_index;
    uint32_t entry_offset;
    EXT2_DIR_ENTRY entry;

    if (!EXT2_ReadInode(volume, dir_inode_number, &dir_inode) || !inode_is_dir(&dir_inode))
    {
        return false;
    }

    if (!find_directory_entry(volume, dir_inode_number, name, name_len, &block_index, &entry_offset, &entry))
    {
        return false;
    }

    if (!read_block(volume, dir_inode.i_block[block_index], g_block_buffer))
    {
        return false;
    }

    entry = *(EXT2_DIR_ENTRY *)(g_block_buffer + entry_offset);
    entry.inode = 0;
    entry.name_len = 0;
    entry.file_type = 0;
    memcpy(g_block_buffer + entry_offset, &entry, (uint32_t)sizeof(EXT2_DIR_ENTRY));
    return write_block(volume, dir_inode.i_block[block_index], g_block_buffer);
}

static bool setup_new_file_inode(EXT2_INODE *inode)
{
    if (!inode)
    {
        return false;
    }

    memset(inode, 0, (uint32_t)sizeof(EXT2_INODE));
    inode->i_mode = EXT2_S_IFREG | 0644u;
    inode->i_links_count = 1u;
    return true;
}

static bool setup_new_dir_inode(EXT2_INODE *inode, EXT2_VOLUME *volume)
{
    if (!inode || !volume)
    {
        return false;
    }

    memset(inode, 0, (uint32_t)sizeof(EXT2_INODE));
    inode->i_mode = EXT2_S_IFDIR | 0755u;
    inode->i_links_count = 2u;
    inode->i_size = volume->block_size;
    inode->i_blocks = volume->sectors_per_block;
    return true;
}

static bool initialize_directory_block(EXT2_VOLUME *volume, uint32_t inode_number, uint32_t parent_inode_number, uint32_t block_number)
{
    EXT2_DIR_ENTRY *dot;
    EXT2_DIR_ENTRY *dotdot;
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;
    uint32_t dot_size = ext2_dir_entry_size(1u);

    if (!volume || volume->block_size < (dot_size + header_size + 1u))
    {
        return false;
    }

    memset(g_block_buffer, 0, volume->block_size);
    dot = (EXT2_DIR_ENTRY *)g_block_buffer;
    dot->inode = inode_number;
    dot->rec_len = (uint16_t)dot_size;
    dot->name_len = 1u;
    dot->file_type = EXT2_FT_DIR;
    memcpy((uint8_t *)dot + header_size, ".", 1u);

    dotdot = (EXT2_DIR_ENTRY *)(g_block_buffer + dot_size);
    dotdot->inode = parent_inode_number;
    dotdot->rec_len = (uint16_t)(volume->block_size - dot_size);
    dotdot->name_len = 2u;
    dotdot->file_type = EXT2_FT_DIR;
    memcpy((uint8_t *)dotdot + header_size, "..", 2u);

    return write_block(volume, block_number, g_block_buffer);
}

static bool lookup_parent_and_name(EXT2_VOLUME *volume, const char *path, uint32_t *parent_inode_out, char *name_out, uint32_t name_out_size, char *parent_path, uint32_t parent_path_size)
{
    uint32_t parent_inode;
    EXT2_INODE parent_inode_cache;

    if (!volume || !path || !parent_inode_out || !name_out || !parent_path)
    {
        return false;
    }

    if (!split_path(path, parent_path, parent_path_size, name_out, name_out_size))
    {
        return false;
    }

    if (EXT2_LookupPath(volume, parent_path, &parent_inode) != EXT2_OK)
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, parent_inode, &parent_inode_cache) || !inode_is_dir(&parent_inode_cache))
    {
        return false;
    }

    *parent_inode_out = parent_inode;
    return true;
}

static bool lookup_child_type(EXT2_VOLUME *volume, uint32_t parent_inode_number, const char *name, uint32_t name_len, uint32_t *child_inode_out, uint8_t *child_type_out)
{
    uint32_t block_index;
    uint32_t entry_offset;
    EXT2_DIR_ENTRY entry;

    if (!find_directory_entry(volume, parent_inode_number, name, name_len, &block_index, &entry_offset, &entry))
    {
        return false;
    }

    if (child_inode_out)
    {
        *child_inode_out = entry.inode;
    }

    if (child_type_out)
    {
        *child_type_out = entry.file_type;
    }

    return true;
}

static bool free_inode_and_blocks(EXT2_VOLUME *volume, uint32_t inode_number, EXT2_INODE *inode)
{
    EXT2_INODE cache;

    if (!volume)
    {
        return false;
    }

    if (inode)
    {
        cache = *inode;
    }
    else if (!EXT2_ReadInode(volume, inode_number, &cache))
    {
        return false;
    }

    if (!free_inode_block_chain(volume, &cache))
    {
        return false;
    }

    return free_inode(volume, inode_number);
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

static EXT2_STATUS find_in_directory(EXT2_VOLUME *volume, uint32_t dir_inode_number, const char *name, uint32_t name_len, uint32_t *inode_out)
{
    EXT2_INODE dir_inode;
    uint32_t block_index;
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;

    if (!inode_out)
    {
        return EXT2_IO_ERROR;
    }

    if (!EXT2_ReadInode(volume, dir_inode_number, &dir_inode) || !inode_is_dir(&dir_inode))
    {
        return EXT2_IO_ERROR;
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
            return EXT2_IO_ERROR;
        }

        while (offset + header_size <= volume->block_size)
        {
            EXT2_DIR_ENTRY *entry = (EXT2_DIR_ENTRY *)(g_block_buffer + offset);
            uint32_t min_size = header_size + entry->name_len;
            const char *entry_name;

            if (entry->rec_len < min_size || entry->rec_len == 0 || offset + entry->rec_len > volume->block_size)
            {
                return EXT2_IO_ERROR;
            }

            entry_name = (const char *)(g_block_buffer + offset + header_size);
            if (entry->inode != 0 && name_len == entry->name_len && memcmp(name, entry_name, name_len) == 0)
            {
                *inode_out = entry->inode;
                return EXT2_OK;
            }

            offset += entry->rec_len;
        }
    }

    return EXT2_NOT_FOUND;
}

static bool resolve_data_block(EXT2_VOLUME *volume, const EXT2_INODE *inode, uint32_t logical_block_index, uint32_t *physical_block_out)
{
    if (logical_block_index < EXT2_NDIR_BLOCKS)
    {
        *physical_block_out = inode->i_block[logical_block_index];
        return true;
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
        return true;
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
    volume->superblock = sb;
    volume->block_size = 1024u << sb.s_log_block_size;
    volume->inode_size = (sb.s_inode_size == 0) ? 128u : sb.s_inode_size;
    volume->first_data_block = sb.s_first_data_block;
    volume->block_count = sb.s_blocks_count;
    volume->inode_count = sb.s_inodes_count;
    volume->first_non_reserved_inode = (sb.s_first_ino == 0) ? 11u : sb.s_first_ino;
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

EXT2_STATUS EXT2_LookupPath(EXT2_VOLUME *volume, const char *path, uint32_t *inode_out)
{
    uint32_t current = EXT2_INODE_ROOT;
    uint32_t i = 0;

    if (!volume || !path || !inode_out)
    {
        return EXT2_IO_ERROR;
    }

    if (path[0] == 0)
    {
        return EXT2_NOT_FOUND;
    }

    while (path[i] == '/')
    {
        i++;
    }

    if (path[i] == 0)
    {
        *inode_out = EXT2_INODE_ROOT;
        return EXT2_OK;
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

        {
            EXT2_STATUS status = find_in_directory(volume, current, &path[start], len, &next_inode);
            if (status != EXT2_OK)
            {
                return status;
            }
        }

        current = next_inode;

        while (path[i] == '/')
        {
            i++;
        }
    }

    *inode_out = current;
    return EXT2_OK;
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

        if (entry->rec_len < header_size)
        {
            return false;
        }

        if ((uint32_t)entry->name_len > (entry->rec_len - header_size))
        {
            return false;
        }

        file->cursor += entry->rec_len;

        if (entry->inode == 0 || entry->name_len == 0)
        {
            continue;
        }

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

    if (EXT2_LookupPath(volume, path, &inode_number) != EXT2_OK)
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

        if (phys_block == 0)
        {
            for (j = 0; j < take; j++)
            {
                out[copied + j] = 0;
            }

            copied += take;
            continue;
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

bool EXT2_CreateFile(EXT2_VOLUME *volume, const char *path)
{
    char parent_path[256];
    char name[256];
    uint32_t parent_inode_number;
    EXT2_INODE parent_inode;
    EXT2_INODE new_inode;
    uint32_t child_inode_number;
    uint32_t name_len;
    uint8_t child_type;

    if (!volume || !path)
    {
        return false;
    }

    if (!lookup_parent_and_name(volume, path, &parent_inode_number, name, sizeof(name), parent_path, sizeof(parent_path)))
    {
        return false;
    }

    name_len = (uint32_t)strlen(name);
    if (lookup_child_type(volume, parent_inode_number, name, name_len, 0, &child_type))
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, parent_inode_number, &parent_inode) || !inode_is_directory(&parent_inode))
    {
        return false;
    }

    if (!alloc_inode(volume, &child_inode_number))
    {
        return false;
    }

    if (!setup_new_file_inode(&new_inode))
    {
        return false;
    }

    if (!write_inode(volume, child_inode_number, &new_inode))
    {
        free_inode(volume, child_inode_number);
        return false;
    }

    if (!append_or_replace_directory_entry(volume, parent_inode_number, &parent_inode, name, name_len, child_inode_number, EXT2_FT_REG_FILE))
    {
        free_inode(volume, child_inode_number);
        return false;
    }

    parent_inode.i_mtime = 0;
    parent_inode.i_ctime = 0;
    write_inode(volume, parent_inode_number, &parent_inode);
    return true;
}

bool EXT2_CreateDir(EXT2_VOLUME *volume, const char *path)
{
    char parent_path[256];
    char name[256];
    uint32_t parent_inode_number;
    EXT2_INODE parent_inode;
    EXT2_INODE new_inode;
    uint32_t child_inode_number;
    uint32_t child_block_number;
    uint32_t name_len;
    uint32_t child_group;

    if (!volume || !path)
    {
        return false;
    }

    if (!lookup_parent_and_name(volume, path, &parent_inode_number, name, sizeof(name), parent_path, sizeof(parent_path)))
    {
        return false;
    }

    name_len = (uint32_t)strlen(name);
    if (lookup_child_type(volume, parent_inode_number, name, name_len, 0, 0))
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, parent_inode_number, &parent_inode) || !inode_is_directory(&parent_inode))
    {
        return false;
    }

    if (!alloc_inode(volume, &child_inode_number))
    {
        return false;
    }

    child_group = (child_inode_number - 1u) / volume->inodes_per_group;

    if (!alloc_block(volume, &child_block_number))
    {
        free_inode(volume, child_inode_number);
        return false;
    }

    if (!setup_new_dir_inode(&new_inode, volume))
    {
        free_block(volume, child_block_number);
        free_inode(volume, child_inode_number);
        return false;
    }

    new_inode.i_block[0] = child_block_number;
    if (!initialize_directory_block(volume, child_inode_number, parent_inode_number, child_block_number))
    {
        free_block(volume, child_block_number);
        free_inode(volume, child_inode_number);
        return false;
    }

    if (!write_inode(volume, child_inode_number, &new_inode))
    {
        free_block(volume, child_block_number);
        free_inode(volume, child_inode_number);
        return false;
    }

    if (!append_or_replace_directory_entry(volume, parent_inode_number, &parent_inode, name, name_len, child_inode_number, EXT2_FT_DIR))
    {
        free_block(volume, child_block_number);
        free_inode(volume, child_inode_number);
        return false;
    }

    parent_inode.i_links_count++;
    parent_inode.i_mtime = 0;
    parent_inode.i_ctime = 0;
    if (!write_inode(volume, parent_inode_number, &parent_inode))
    {
        return false;
    }

    update_group_and_super_counts(volume, child_group, 0, 0, 1);
    return true;
}

bool EXT2_RemoveFile(EXT2_VOLUME *volume, const char *path)
{
    char parent_path[256];
    char name[256];
    uint32_t parent_inode_number;
    uint32_t child_inode_number;
    uint8_t child_type;
    uint32_t name_len;

    if (!volume || !path)
    {
        return false;
    }

    if (!lookup_parent_and_name(volume, path, &parent_inode_number, name, sizeof(name), parent_path, sizeof(parent_path)))
    {
        return false;
    }

    name_len = (uint32_t)strlen(name);
    if (!lookup_child_type(volume, parent_inode_number, name, name_len, &child_inode_number, &child_type) || child_type != EXT2_FT_REG_FILE)
    {
        return false;
    }

    if (!remove_directory_entry(volume, parent_inode_number, name, name_len))
    {
        return false;
    }

    if (!free_inode_and_blocks(volume, child_inode_number, 0))
    {
        return false;
    }

    return true;
}

bool EXT2_RemoveDir(EXT2_VOLUME *volume, const char *path)
{
    char parent_path[256];
    char name[256];
    uint32_t parent_inode_number;
    uint32_t child_inode_number;
    uint8_t child_type;
    EXT2_INODE child_inode;
    EXT2_INODE parent_inode;
    uint32_t name_len;
    uint32_t child_group;

    if (!volume || !path)
    {
        return false;
    }

    if (!lookup_parent_and_name(volume, path, &parent_inode_number, name, sizeof(name), parent_path, sizeof(parent_path)))
    {
        return false;
    }

    name_len = (uint32_t)strlen(name);
    if (!lookup_child_type(volume, parent_inode_number, name, name_len, &child_inode_number, &child_type) || child_type != EXT2_FT_DIR)
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, child_inode_number, &child_inode) || !inode_is_directory(&child_inode))
    {
        return false;
    }

    if (!directory_is_empty(volume, child_inode_number))
    {
        return false;
    }

    if (!remove_directory_entry(volume, parent_inode_number, name, name_len))
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, parent_inode_number, &parent_inode))
    {
        return false;
    }

    if (parent_inode.i_links_count > 0)
    {
        parent_inode.i_links_count--;
        parent_inode.i_mtime = 0;
        parent_inode.i_ctime = 0;
        write_inode(volume, parent_inode_number, &parent_inode);
    }

    child_group = (child_inode_number - 1u) / volume->inodes_per_group;
    if (!free_inode_and_blocks(volume, child_inode_number, &child_inode))
    {
        return false;
    }

    update_group_and_super_counts(volume, child_group, 0, 0, -1);
    return true;
}

bool EXT2_Rename(EXT2_VOLUME *volume, const char *old_path, const char *new_path)
{
    char old_parent_path[256];
    char old_name[256];
    char new_parent_path[256];
    char new_name[256];
    uint32_t old_parent_inode_number;
    uint32_t new_parent_inode_number;
    uint32_t child_inode_number;
    uint8_t child_type;
    EXT2_INODE old_parent_inode;
    EXT2_INODE new_parent_inode;
    uint32_t old_name_len;
    uint32_t new_name_len;
    uint32_t child_group;

    if (!volume || !old_path || !new_path)
    {
        return false;
    }

    if (!lookup_parent_and_name(volume, old_path, &old_parent_inode_number, old_name, sizeof(old_name), old_parent_path, sizeof(old_parent_path)))
    {
        return false;
    }

    if (!lookup_parent_and_name(volume, new_path, &new_parent_inode_number, new_name, sizeof(new_name), new_parent_path, sizeof(new_parent_path)))
    {
        return false;
    }

    old_name_len = (uint32_t)strlen(old_name);
    new_name_len = (uint32_t)strlen(new_name);

    if (!lookup_child_type(volume, old_parent_inode_number, old_name, old_name_len, &child_inode_number, &child_type))
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, old_parent_inode_number, &old_parent_inode) || !inode_is_directory(&old_parent_inode))
    {
        return false;
    }

    if (!EXT2_ReadInode(volume, new_parent_inode_number, &new_parent_inode) || !inode_is_directory(&new_parent_inode))
    {
        return false;
    }

    if (lookup_child_type(volume, new_parent_inode_number, new_name, new_name_len, 0, 0))
    {
        return false;
    }

    if (old_parent_inode_number == new_parent_inode_number)
    {
        if (update_directory_entry_name(volume, old_parent_inode_number, old_name, old_name_len, new_name, new_name_len))
        {
            return true;
        }
    }

    if (!append_or_replace_directory_entry(volume, new_parent_inode_number, &new_parent_inode, new_name, new_name_len, child_inode_number, child_type))
    {
        return false;
    }

    if (child_type == EXT2_FT_DIR && old_parent_inode_number != new_parent_inode_number)
    {
        if (!update_directory_parent_link(volume, child_inode_number, new_parent_inode_number))
        {
            remove_directory_entry(volume, new_parent_inode_number, new_name, new_name_len);
            return false;
        }

        child_group = (child_inode_number - 1u) / volume->inodes_per_group;
        if (old_parent_inode.i_links_count > 0)
        {
            old_parent_inode.i_links_count--;
        }

        new_parent_inode.i_links_count++;
        write_inode(volume, old_parent_inode_number, &old_parent_inode);
        write_inode(volume, new_parent_inode_number, &new_parent_inode);
        update_group_and_super_counts(volume, child_group, 0, 0, 0);
    }

    if (!remove_directory_entry(volume, old_parent_inode_number, old_name, old_name_len))
    {
        return false;
    }

    return true;
}

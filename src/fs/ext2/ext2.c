#include "ext2.h"
#include "../../common/memory.h"
#include "../../common/string.h"
#include "../../common/stdio.h"

#define EXT2_SECTOR_SIZE 512u
#define EXT2_MIN(a, b) ((a) < (b) ? (a) : (b))
#define OFFSETOF(type, member) ((uint32_t)&(((type *)0)->member))

static uint8_t g_sector_buffer[EXT2_SECTOR_SIZE];
static uint8_t g_block_buffer[EXT2_MAX_BLOCK_SIZE];
static uint8_t g_block_buffer2[EXT2_MAX_BLOCK_SIZE]; // scratch for single-indirect block
static uint8_t g_block_buffer3[EXT2_MAX_BLOCK_SIZE]; // scratch for double-indirect block (first level)
static uint8_t g_block_buffer4[EXT2_MAX_BLOCK_SIZE]; // scratch for triple-indirect block (first level)
static uint8_t g_inode_buffer[EXT2_MAX_INODE_SIZE];
static uint8_t g_bitmap_buffer[EXT2_MAX_BLOCK_SIZE];

static bool inode_is_dir(const EXT2_INODE *inode);
static bool inode_is_regular(const EXT2_INODE *inode);
static bool inode_is_directory(const EXT2_INODE *inode);

/**
 * reads bytes from the disk at the specified absolute byte offset and size into the provided output buffer.
 * @param disk pointer to the block device representing the disk.
 * @param byte_offset the absolute byte offset on the disk to start reading from.
 * @param size the number of bytes to read.
 * @param out pointer to the output buffer where the read bytes will be stored.
 * @return true if the read operation was successful, false otherwise.
 */
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
/**
 * reads a block from the EXT2 volume into the provided output buffer.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param block the block number to read from the volume.
 * @param out pointer to the output buffer where the read block will be stored.
 * @return true if the read operation was successful, false otherwise.
 */
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

/**
 * reads the block group descriptor for the specified group from the EXT2 volume into the provided output structure.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param group the block group number to read the descriptor for.
 * @param out_desc pointer to the EXT2_BLOCK_GROUP_DESC structure where the read descriptor will be stored.
 * @return true if the read operation was successful, false otherwise.
 */
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

/**
 * aligns the given value to the next multiple of 4.
 * @param value the value to align.
 * @return the aligned value, which is the smallest multiple of 4 that is greater than or equal to the input value.
 */
static uint32_t ext2_align4(uint32_t value)
{
    return (value + 3u) & ~3u;
}

/**
 * calculates the size of a directory entry based on the length of the name.
 * @param name_len the length of the name field in bytes.
 * @return the size of the directory entry in bytes, aligned to a 4-byte boundary.
 */
static uint32_t ext2_dir_entry_size(uint32_t name_len)
{
    return ext2_align4(OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1u + name_len);
}

/**
 * writes bytes to the disk at the specified absolute byte offset and size from the provided input buffer.
 * @param disk pointer to the block device representing the disk.
 * @param byte_offset the absolute byte offset on the disk to start writing to.
 * @param size the number of bytes to write.
 * @param in pointer to the input buffer containing the bytes to be written.
 * @return true if the write operation was successful, false otherwise.
 */
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

/**
 * writes a block to the EXT2 volume from the provided input buffer.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param block the block number to write to the volume.
 * @param in pointer to the input buffer containing the block data to be written.
 * @return true if the write operation was successful, false otherwise.
 */
static bool write_block(EXT2_VOLUME *volume, uint32_t block, const void *in)
{
    if (!volume || !volume->disk || !in || volume->sectors_per_block == 0)
    {
        return false;
    }

    return block_device_write(volume->disk, block * volume->sectors_per_block, (uint8_t)volume->sectors_per_block, in);
}

/**
 * writes the block group descriptor for the specified group to the EXT2 volume from the provided input structure.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param group the block group number to write the descriptor for.
 * @param desc pointer to the EXT2_BLOCK_GROUP_DESC structure containing the descriptor data to be written.
 * @return true if the write operation was successful, false otherwise.
 */
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

/**
 * writes the superblock to the EXT2 volume.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @return true if the write operation was successful, false otherwise.
 */
static bool write_superblock(EXT2_VOLUME *volume)
{
    if (!volume)
    {
        return false;
    }

    return write_abs_bytes(volume->disk, EXT2_SUPERBLOCK_OFFSET, (uint32_t)sizeof(EXT2_SUPERBLOCK), &volume->superblock);
}

/**
 * reads the inode bitmap for the specified block group from the EXT2 volume into the provided output buffer.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param group the block group number to read the inode bitmap for.
 * @param bitmap pointer to the output buffer where the read inode bitmap will be stored.
 * @return true if the read operation was successful, false otherwise.
 */
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

/**
 * writes the inode bitmap for the specified block group to the EXT2 volume from the provided input buffer.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param group the block group number to write the inode bitmap for.
 * @param bitmap pointer to the input buffer containing the inode bitmap data to be written.
 * @return true if the write operation was successful, false otherwise.
 */
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

/**
 * reads the block bitmap for the specified block group from the EXT2 volume into the provided output buffer.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param group the block group number to read the block bitmap for.
 * @param bitmap pointer to the output buffer where the read block bitmap will be stored.
 * @return true if the read operation was successful, false otherwise.
 */
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
/**
 * writes the block bitmap for the specified block group to the EXT2 volume from the provided input buffer.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param group the block group number to write the block bitmap for.
 * @param bitmap pointer to the input buffer containing the block bitmap data to be written.
 * @return true if the write operation was successful, false otherwise.
 */
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

/**
 * tests whether a specific bit in the bitmap is set (1) or not (0).
 * @param bitmap pointer to the bitmap array.
 * @param index the index of the bit to test.
 * @return true if the bit is set (1), false if it is not set (0).
 */
static bool bitmap_test(const uint8_t *bitmap, uint32_t index)
{
    return (bitmap[index / 8u] & (uint8_t)(1u << (index % 8u))) != 0;
}

/**
 * sets or clears a specific bit in the bitmap based on the provided value.
 * @param bitmap pointer to the bitmap array.
 * @param index the index of the bit to set or clear.
 * @param value true to set the bit (1), false to clear the bit (0).
 */
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

/**
 * finds the first free (unset) bit in the bitmap starting from a specified index.
 * @param bitmap pointer to the bitmap array.
 * @param bit_count the total number of bits in the bitmap to consider.
 * @param start_bit the index to start searching for a free bit.
 * @param bit_out pointer to a variable where the index of the found free bit will be stored.
 * @return true if a free bit was found and its index is stored in bit_out, false if no free bit was found.
 */
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

/**
 * determines the location of a specific inode within the EXT2 volume, including its block group descriptor and byte offset.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param inode_number the inode number to locate (1-based index).
 * @param desc_out pointer to an EXT2_BLOCK_GROUP_DESC structure where the block group descriptor will be stored.
 * @param inode_byte_offset_out pointer to a variable where the byte offset of the inode within the volume will be stored.
 * @param group_out optional pointer to a variable where the block group number will be stored (can be NULL if not needed).
 * @return true if the inode location was successfully determined, false otherwise.
 */
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

/**
 * writes an inode to the EXT2 volume at the specified inode number.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param inode_number the inode number to write to (1-based index).
 * @param inode pointer to the EXT2_INODE structure containing the inode data to be written.
 * @return true if the write operation was successful, false otherwise.
 */
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

/**
 * updates the free block count, free inode count, and used directory count in both the block group descriptor and the superblock for the specified block group.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param group the block group number to update.
 * @param free_blocks_delta the change in the number of free blocks (positive to increase, negative to decrease).
 * @param free_inodes_delta the change in the number of free inodes (positive to increase, negative to decrease).
 * @param used_dirs_delta the change in the number of used directories (positive to increase, negative to decrease).
 * @return true if the update operation was successful, false otherwise.
 */
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

/**
 * allocates a free inode from the EXT2 volume and returns its inode number.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param inode_number_out pointer to a variable where the allocated inode number will be stored (1-based index).
 * @return true if an inode was successfully allocated and its number is stored in inode_number_out, false if no free inode was available or an error occurred.
 */
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

/**
 * frees an allocated inode in the EXT2 volume, marking it as available for future use.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param inode_number the inode number to free (1-based index).
 * @return true if the inode was successfully freed, false if the inode number was invalid or an error occurred during the operation.
 */
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

/**
 * allocates a free block from the EXT2 volume and returns its block number.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param block_number_out pointer to a variable where the allocated block number will be stored.
 * @return true if a block was successfully allocated and its number is stored in block_number_out, false if no free block was available or an error occurred.
 */
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

/**
 * frees an allocated block in the EXT2 volume, marking it as available for future use.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param block_number the block number to free.
 * @return true if the block was successfully freed, false if the block number was invalid or an error occurred during the operation.
 */
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

/**
 * frees all blocks associated with an inode, including direct, single indirect, double indirect, and triple indirect blocks.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param inode pointer to the EXT2_INODE structure representing the inode whose blocks are to be freed.
 * @return true if all blocks were successfully freed, false if an error occurred during the operation or if the volume or inode pointers were invalid.
 */
static bool free_inode_block_chain(EXT2_VOLUME *volume, EXT2_INODE *inode)
{
    uint32_t i;
    uint32_t ptrs_per_block;

    if (!volume || !inode)
        return false;

    ptrs_per_block = volume->block_size / 4u;

    // free direct blocks
    for (i = 0; i < EXT2_NDIR_BLOCKS; i++)
    {
        if (inode->i_block[i] != 0)
        {
            if (!free_block(volume, inode->i_block[i]))
                return false;

            inode->i_block[i] = 0;
        }
    }

    // free single indirect: i_block[12] -> block of pointers -> data blocks
    if (inode->i_block[12] != 0)
    {
        uint32_t *entries;
        uint32_t j;

        if (!read_block(volume, inode->i_block[12], g_block_buffer))
            return false;

        entries = (uint32_t *)g_block_buffer;
        for (j = 0; j < ptrs_per_block; j++)
        {
            if (entries[j] != 0 && !free_block(volume, entries[j]))
                return false;
        }

        if (!free_block(volume, inode->i_block[12]))
            return false;

        inode->i_block[12] = 0;
    }

    // free double indirect: i_block[13] -> L1 block -> L2 blocks -> data blocks
    if (inode->i_block[13] != 0)
    {
        uint32_t l1;
        uint32_t *l1_entries;

        if (!read_block(volume, inode->i_block[13], g_block_buffer3))
            return false;

        l1_entries = (uint32_t *)g_block_buffer3;
        for (l1 = 0; l1 < ptrs_per_block; l1++)
        {
            if (l1_entries[l1] != 0)
            {
                uint32_t *l2_entries;
                uint32_t l2;

                if (!read_block(volume, l1_entries[l1], g_block_buffer))
                    return false;

                l2_entries = (uint32_t *)g_block_buffer;
                for (l2 = 0; l2 < ptrs_per_block; l2++)
                {
                    if (l2_entries[l2] != 0 && !free_block(volume, l2_entries[l2]))
                        return false;
                }

                if (!free_block(volume, l1_entries[l1]))
                    return false;
            }
        }

        if (!free_block(volume, inode->i_block[13]))
            return false;

        inode->i_block[13] = 0;
    }

    // free triple indirect: i_block[14] -> L1 block -> L2 blocks -> L3 blocks -> data blocks
    if (inode->i_block[14] != 0)
    {
        uint32_t *l1_entries;
        uint32_t l1;

        if (!read_block(volume, inode->i_block[14], g_block_buffer4))
            return false;

        l1_entries = (uint32_t *)g_block_buffer4;
        for (l1 = 0; l1 < ptrs_per_block; l1++)
        {
            if (l1_entries[l1] != 0)
            {
                uint32_t *l2_entries;
                uint32_t l2;

                if (!read_block(volume, l1_entries[l1], g_block_buffer3))
                    return false;

                l2_entries = (uint32_t *)g_block_buffer3;
                for (l2 = 0; l2 < ptrs_per_block; l2++)
                {
                    if (l2_entries[l2] != 0)
                    {
                        uint32_t *l3_entries;
                        uint32_t l3;

                        if (!read_block(volume, l2_entries[l2], g_block_buffer))
                            return false;

                        l3_entries = (uint32_t *)g_block_buffer;
                        for (l3 = 0; l3 < ptrs_per_block; l3++)
                        {
                            if (l3_entries[l3] != 0 && !free_block(volume, l3_entries[l3]))
                                return false;
                        }

                        if (!free_block(volume, l2_entries[l2]))
                            return false;
                    }
                }

                if (!free_block(volume, l1_entries[l1]))
                    return false;
            }
        }

        if (!free_block(volume, inode->i_block[14]))
            return false;

        inode->i_block[14] = 0;
    }

    return true;
}

/**
 * splits a given path into its parent directory and the name of the final component (file or directory).
 * @param path the input path string to be split (must start with '/').
 * @param parent buffer to store the parent directory path.
 * @param parent_size the size of the parent buffer in bytes.
 * @param name buffer to store the name of the final component.
 * @param name_size the size of the name buffer in bytes.
 * @return true if the path was successfully split into parent and name, false if the path was invalid or the buffers were too small to hold the results.
 */
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

/**
 * searches for a directory entry with a specific name within a given directory inode, returning the block index, offset, and entry details if found.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param dir_inode_number the inode number of the directory to search within.
 * @param name the name of the directory entry to search for.
 * @param name_len the length of the name to search for.
 * @param block_index_out pointer to a variable where the block index of the found entry will be stored.
 * @param offset_out pointer to a variable where the byte offset of the found entry within the block will be stored.
 * @param entry_out pointer to an EXT2_DIR_ENTRY structure where the details of the found entry will be stored.
 * @return true if the directory entry was found and its details are stored in the output parameters, false if the entry was not found or an error occurred during the search.
 */
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

/**
 * appends a new directory entry to a parent directory inode or replaces an existing entry with the same name, ensuring proper space allocation and alignment within the directory's data blocks.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param parent_inode_number the inode number of the parent directory where the entry will be added or replaced.
 * @param parent_inode pointer to the EXT2_INODE structure representing the parent directory inode.
 * @param name the name of the directory entry to add or replace.
 * @param name_len the length of the name of the directory entry.
 * @param child_inode_number the inode number of the child entry to be added or replaced.
 * @param file_type the type of the file (e.g., regular file, directory) for the new directory entry.
 * @return true if the directory entry was successfully appended or replaced,
 */
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

/**
 * updates the name of an existing directory entry within a specified directory inode, ensuring that the new name fits within the allocated space for the entry.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param dir_inode_number the inode number of the directory containing the entry to be renamed.
 * @param old_name the current name of the directory entry to be renamed.
 * @param old_name_len the length of the current name of the directory entry.
 * @param new_name the new name to assign to the directory entry.
 * @param new_name_len the length of the new name to assign to the directory entry.
 * @return true if the directory entry name was successfully updated, false if the entry was not found, the new name does not fit, or an error occurred during the operation.
 */
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

/**
 * checks if a directory represented by a given inode number is empty, meaning it contains no entries other than the standard '.' and '..' entries.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param inode_number the inode number of the directory to check.
 * @return true if the directory is empty (only contains '.' and '..'), false if it contains other entries or if an error occurred during the check.
 */
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

/**
 * updates the '..' entry in a directory to point to a new parent inode number, effectively changing the parent directory reference for that directory.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param dir_inode_number the inode number of the directory whose parent link is to be updated.
 * @param parent_inode_number the inode number of the new parent directory to which the '..' entry should point.
 * @return true if the parent link was successfully updated, false if the directory inode could not be read, the directory is invalid, or an error occurred during the update process.
 */
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

/**
 * removes a directory entry with a specific name from a given directory inode, effectively deleting the entry from the directory's data blocks.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param dir_inode_number the inode number of the directory from which the entry will be removed.
 * @param name the name of the directory entry to be removed.
 * @param name_len the length of the name of the directory entry to be removed.
 * @return true if the directory entry was successfully removed, false if the entry was not found, the directory inode could not be read, or an error occurred during the removal process.
 */
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

/**
 * initializes a new inode structure for a regular file, setting its mode, link count, and clearing other fields to prepare it for use in the filesystem.
 * @param inode pointer to the EXT2_INODE structure to be initialized.
 * @return true if the inode was successfully initialized, false if the inode pointer was null.
 */
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

/**
 * initializes a new inode structure for a directory, setting its mode, link count, size, and block count to prepare it for use in the filesystem.
 * @param inode pointer to the EXT2_INODE structure to be initialized.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume, used to determine the block size and sectors per block for the directory inode.
 * @return true if the inode was successfully initialized, false if either the inode or volume pointers were null.
 */
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

/**
 * initializes a new directory block with the standard '.' and '..' entries, setting their inode numbers and ensuring proper formatting for the directory structure.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param inode_number the inode number of the directory being initialized (for the '.' entry).
 * @param parent_inode_number the inode number of the parent directory (for the '..' entry).
 * @param block_number the block number where the new directory block will be written.
 * @return true if the directory block was successfully initialized and written to the specified block number, false if an error occurred during the initialization or writing process, or if the volume pointer was null or the block size was insufficient to hold the directory entries.
 */
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

/**
 * looks up the parent directory inode and the name of the final component in a given path, splitting the path into its parent and child components and verifying that the parent is a valid directory.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param path the input path string to be looked up (must start with '/').
 * @param parent_inode_out pointer to a variable where the inode number of the parent directory will be stored.
 * @param name_out buffer to store the name of the final component (file or directory) in the path.
 * @param name_out_size the size of the name_out buffer in bytes.
 * @param parent_path buffer to store the parent directory path.
 * @param parent_path_size the size of the parent_path buffer in bytes.
 * @return true if the parent inode and name were successfully looked up and stored in the output parameters, false if the path was invalid, the parent directory could not be found, or the parent inode is not a valid directory.
 */
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

/**
 * looks up a child directory entry by name within a specified parent directory inode, returning the child's inode number and file type if found.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param parent_inode_number the inode number of the parent directory to search within.
 * @param name the name of the child directory entry to look up.
 * @param name_len the length of the name of the child directory entry.
 * @param child_inode_out pointer to a variable where the inode number of the found child entry will be stored.
 * @param child_type_out pointer to a variable where the file type of the found child entry will be stored.
 * @return true if the child directory entry was found and its details are stored in the output parameters, false if the entry was not found or an error occurred during the lookup process.
 */
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

/**
 * frees the blocks associated with a given inode and then frees the inode itself, effectively removing the file or directory represented by that inode from the filesystem.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param inode_number the inode number of the file or directory to be freed.
 * @param inode pointer to the EXT2_INODE structure representing the inode to be freed. If null, the function will read the inode from the filesystem.
 * @return true if the blocks and inode were successfully freed, false if an error occurred during the process or if the volume pointer was null.
 */
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

/**
 * determines if a given inode represents a directory by checking its mode against the directory file type constant.
 * @param inode pointer to the EXT2_INODE structure to be checked.
 * @return true if the inode represents a directory, false otherwise.
 */
static bool inode_is_dir(const EXT2_INODE *inode)
{
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

/**
 * determines if a given inode represents a regular file by checking its mode against the regular file type constant.
 * @param inode pointer to the EXT2_INODE structure to be checked.
 * @return true if the inode represents a regular file, false otherwise.
 */
static bool inode_is_regular(const EXT2_INODE *inode)
{
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG;
}

/**
 * determines if a given inode represents a directory by checking its mode against the directory file type constant.
 * @param inode pointer to the EXT2_INODE structure to be checked.
 * @return true if the inode represents a directory, false otherwise.
 */
static bool inode_is_directory(const EXT2_INODE *inode)
{
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

/**
 * maps an inode's mode to the corresponding EXT2 file type constant, returning the appropriate value for directory or regular file types, or 0 for unsupported types.
 * @param inode pointer to the EXT2_INODE structure whose file type is to be determined.
 * @return the EXT2 file type constant corresponding to the inode's mode, or 0 if the inode does not represent a directory or regular file.
 */
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

/**
 * searches for a directory entry with a specific name within a given directory inode, returning the inode number of the found entry if it exists.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param dir_inode_number the inode number of the directory to search within.
 * @param name the name of the directory entry to search for.
 * @param name_len the length of the name to search for.
 * @param inode_out pointer to a variable where the inode number of the found entry will be stored if the entry is found.
 * @return EXT2_OK if the entry was found and its inode number is stored in inode_out, EXT2_NOT_FOUND if the entry does not exist, or EXT2_IO_ERROR if an error occurred during the search or if the directory inode could not be read.
 */
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

/**
 * resolves a logical block index within an inode to its corresponding physical block number on disk, handling direct, single indirect, double indirect, and triple indirect block addressing as defined by the EXT2 filesystem structure.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param inode pointer to the EXT2_INODE structure representing the inode whose block is being resolved.
 * @param logical_block_index the logical block index to resolve (0-based).
 * @param physical_block_out pointer to a variable where the resolved physical block number will be stored.
 * @return true if the logical block index was successfully resolved to a physical block number, false if an error occurred during the resolution process or if the logical block index is out of range for the given inode.
 */
static bool resolve_data_block(EXT2_VOLUME *volume, const EXT2_INODE *inode, uint32_t logical_block_index, uint32_t *physical_block_out)
{
    uint32_t ptrs_per_block = volume->block_size / 4u;

    // direct blocks: i_block[0..11]
    if (logical_block_index < EXT2_NDIR_BLOCKS)
    {
        *physical_block_out = inode->i_block[logical_block_index];
        return true;
    }

    logical_block_index -= EXT2_NDIR_BLOCKS;

    // single indirect: i_block[12] -> block of pointers -> data block
    if (logical_block_index < ptrs_per_block)
    {
        uint32_t indirect = inode->i_block[12];

        if (indirect == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, indirect, g_block_buffer2))
            return false;

        *physical_block_out = ((uint32_t *)g_block_buffer2)[logical_block_index];
        return true;
    }

    logical_block_index -= ptrs_per_block;

    // double indirect: i_block[13] -> L1 block of pointers -> L2 block of pointers -> data block
    if (logical_block_index < ptrs_per_block * ptrs_per_block)
    {
        uint32_t l1_index = logical_block_index / ptrs_per_block;
        uint32_t l2_index = logical_block_index % ptrs_per_block;
        uint32_t l1_block;
        uint32_t l2_block;

        if (inode->i_block[13] == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, inode->i_block[13], g_block_buffer3))
            return false;

        l1_block = ((uint32_t *)g_block_buffer3)[l1_index];
        if (l1_block == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, l1_block, g_block_buffer2))
            return false;

        l2_block = ((uint32_t *)g_block_buffer2)[l2_index];
        *physical_block_out = l2_block;
        return true;
    }

    logical_block_index -= ptrs_per_block * ptrs_per_block;

    // triple indirect: i_block[14] -> L1 block -> L2 block -> L3 block -> data block
    {
        uint32_t l1_index = logical_block_index / (ptrs_per_block * ptrs_per_block);
        uint32_t remainder = logical_block_index % (ptrs_per_block * ptrs_per_block);
        uint32_t l2_index = remainder / ptrs_per_block;
        uint32_t l3_index = remainder % ptrs_per_block;
        uint32_t l1_block;
        uint32_t l2_block;
        uint32_t l3_block;

        if (inode->i_block[14] == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, inode->i_block[14], g_block_buffer4))
            return false;

        l1_block = ((uint32_t *)g_block_buffer4)[l1_index];
        if (l1_block == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, l1_block, g_block_buffer3))
            return false;

        l2_block = ((uint32_t *)g_block_buffer3)[l2_index];
        if (l2_block == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, l2_block, g_block_buffer2))
            return false;

        l3_block = ((uint32_t *)g_block_buffer2)[l3_index];
        *physical_block_out = l3_block;
        return true;
    }
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

/**
 * reads the next directory entry from an open directory file, populating the provided EXT2_DIRECTORY_ENTRY structure with the entry's details if successful.
 * @param file pointer to the EXT2_FILE structure representing the open directory file.
 * @param entryOut pointer to the EXT2_DIRECTORY_ENTRY structure where the read entry's details will be stored.
 * @return true if a directory entry was successfully read and stored in entryOut, false if the end of the directory was reached, the file is not a directory, or an error occurred during the read operation.
 */
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

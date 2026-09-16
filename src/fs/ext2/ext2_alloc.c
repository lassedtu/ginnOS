#include "ext2_internal.h"

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
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param inode_number the inode number to locate (1-based index).
 * @param desc_out pointer to an ext2_block_group_desc_t structure where the block group descriptor will be stored.
 * @param inode_byte_offset_out pointer to a variable where the byte offset of the inode within the volume will be stored.
 * @param group_out optional pointer to a variable where the block group number will be stored (can be NULL if not needed).
 * @return true if the inode location was successfully determined, false otherwise.
 */
bool inode_location(ext2_volume_t *volume, uint32_t inode_number, ext2_block_group_desc_t *desc_out, uint32_t *inode_byte_offset_out, uint32_t *group_out)
{
    uint32_t zero_based;
    uint32_t group;
    uint32_t index;
    ext2_block_group_desc_t desc;

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
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param inode_number the inode number to write to (1-based index).
 * @param inode pointer to the ext2_inode_t structure containing the inode data to be written.
 * @return true if the write operation was successful, false otherwise.
 */
bool write_inode(ext2_volume_t *volume, uint32_t inode_number, const ext2_inode_t *inode)
{
    ext2_block_group_desc_t desc;
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
    memcpy(inode_buffer, inode, sizeof(ext2_inode_t));
    return write_abs_bytes(volume->disk, inode_byte_offset, volume->inode_size, inode_buffer, volume->buf_sector);
}

/**
 * updates the free block count, free inode count, and used directory count in both the block group descriptor and the superblock for the specified block group.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param group the block group number to update.
 * @param free_blocks_delta the change in the number of free blocks (positive to increase, negative to decrease).
 * @param free_inodes_delta the change in the number of free inodes (positive to increase, negative to decrease).
 * @param used_dirs_delta the change in the number of used directories (positive to increase, negative to decrease).
 * @return true if the update operation was successful, false otherwise.
 */
bool update_group_and_super_counts(ext2_volume_t *volume, uint32_t group, int32_t free_blocks_delta, int32_t free_inodes_delta, int32_t used_dirs_delta)
{
    ext2_block_group_desc_t desc;
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
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param inode_number_out pointer to a variable where the allocated inode number will be stored (1-based index).
 * @return true if an inode was successfully allocated and its number is stored in inode_number_out, false if no free inode was available or an error occurred.
 */
bool alloc_inode(ext2_volume_t *volume, uint32_t *inode_number_out)
{
    uint32_t group;

    if (!volume || !inode_number_out)
    {
        return false;
    }

    for (group = 0; group < volume->block_group_count; group++)
    {
        ext2_block_group_desc_t desc;
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

        if (!read_inode_bitmap(volume, group, volume->buf_bitmap))
        {
            return false;
        }

        if (!bitmap_find_free(volume->buf_bitmap, bit_limit, start_bit, &free_bit))
        {
            continue;
        }

        bitmap_set(volume->buf_bitmap, free_bit, true);
        if (!write_inode_bitmap(volume, group, volume->buf_bitmap))
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
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param inode_number the inode number to free (1-based index).
 * @return true if the inode was successfully freed, false if the inode number was invalid or an error occurred during the operation.
 */
bool free_inode(ext2_volume_t *volume, uint32_t inode_number)
{
    ext2_block_group_desc_t desc;
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

    if (!read_inode_bitmap(volume, group, volume->buf_bitmap))
    {
        return false;
    }

    bitmap_set(volume->buf_bitmap, index, false);
    if (!write_inode_bitmap(volume, group, volume->buf_bitmap))
    {
        return false;
    }

    return update_group_and_super_counts(volume, group, 0, 1, 0);
}

/**
 * allocates a free block from the EXT2 volume and returns its block number.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param block_number_out pointer to a variable where the allocated block number will be stored.
 * @return true if a block was successfully allocated and its number is stored in block_number_out, false if no free block was available or an error occurred.
 */
bool alloc_block(ext2_volume_t *volume, uint32_t *block_number_out)
{
    uint32_t group;

    if (!volume || !block_number_out)
    {
        return false;
    }

    for (group = 0; group < volume->block_group_count; group++)
    {
        ext2_block_group_desc_t desc;
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

        if (!read_block_bitmap(volume, group, volume->buf_bitmap))
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

        if (!bitmap_find_free(volume->buf_bitmap, bit_limit, 0, &free_bit))
        {
            continue;
        }

        bitmap_set(volume->buf_bitmap, free_bit, true);
        if (!write_block_bitmap(volume, group, volume->buf_bitmap))
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
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param block_number the block number to free.
 * @return true if the block was successfully freed, false if the block number was invalid or an error occurred during the operation.
 */
bool free_block(ext2_volume_t *volume, uint32_t block_number)
{
    ext2_block_group_desc_t desc;
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

    if (!read_block_bitmap(volume, group, volume->buf_bitmap))
    {
        return false;
    }

    bitmap_set(volume->buf_bitmap, relative % volume->blocks_per_group, false);
    if (!write_block_bitmap(volume, group, volume->buf_bitmap))
    {
        return false;
    }

    return update_group_and_super_counts(volume, group, 1, 0, 0);
}

/**
 * frees all blocks associated with an inode, including direct, single indirect, double indirect, and triple indirect blocks.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param inode pointer to the ext2_inode_t structure representing the inode whose blocks are to be freed.
 * @return true if all blocks were successfully freed, false if an error occurred during the operation or if the volume or inode pointers were invalid.
 */
bool free_inode_block_chain(ext2_volume_t *volume, ext2_inode_t *inode)
{
    uint32_t i;
    uint32_t ptrs_per_block;

    if (!volume || !inode)
        return false;

    ptrs_per_block = volume->block_size / sizeof(uint32_t);

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

        if (!read_block(volume, inode->i_block[12], volume->buf_block))
            return false;

        entries = (uint32_t *)volume->buf_block;
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

        if (!read_block(volume, inode->i_block[13], volume->buf_block3))
            return false;

        l1_entries = (uint32_t *)volume->buf_block3;
        for (l1 = 0; l1 < ptrs_per_block; l1++)
        {
            if (l1_entries[l1] != 0)
            {
                uint32_t *l2_entries;
                uint32_t l2;

                if (!read_block(volume, l1_entries[l1], volume->buf_block))
                    return false;

                l2_entries = (uint32_t *)volume->buf_block;
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

        if (!read_block(volume, inode->i_block[14], volume->buf_block4))
            return false;

        l1_entries = (uint32_t *)volume->buf_block4;
        for (l1 = 0; l1 < ptrs_per_block; l1++)
        {
            if (l1_entries[l1] != 0)
            {
                uint32_t *l2_entries;
                uint32_t l2;

                if (!read_block(volume, l1_entries[l1], volume->buf_block3))
                    return false;

                l2_entries = (uint32_t *)volume->buf_block3;
                for (l2 = 0; l2 < ptrs_per_block; l2++)
                {
                    if (l2_entries[l2] != 0)
                    {
                        uint32_t *l3_entries;
                        uint32_t l3;

                        if (!read_block(volume, l2_entries[l2], volume->buf_block))
                            return false;

                        l3_entries = (uint32_t *)volume->buf_block;
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

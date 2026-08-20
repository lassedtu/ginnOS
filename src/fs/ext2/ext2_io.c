#include "ext2_internal.h"

// global buffer definitions
uint8_t g_sector_buffer[EXT2_SECTOR_SIZE];
uint8_t g_block_buffer[EXT2_MAX_BLOCK_SIZE];
uint8_t g_block_buffer2[EXT2_MAX_BLOCK_SIZE]; // scratch for single-indirect block
uint8_t g_block_buffer3[EXT2_MAX_BLOCK_SIZE]; // scratch for double-indirect block (first level)
uint8_t g_block_buffer4[EXT2_MAX_BLOCK_SIZE]; // scratch for triple-indirect block (first level)
uint8_t g_inode_buffer[EXT2_MAX_INODE_SIZE];
uint8_t g_bitmap_buffer[EXT2_MAX_BLOCK_SIZE];

/**
 * reads bytes from the disk at the specified absolute byte offset and size into the provided output buffer.
 * @param disk pointer to the block device representing the disk.
 * @param byte_offset the absolute byte offset on the disk to start reading from.
 * @param size the number of bytes to read.
 * @param out pointer to the output buffer where the read bytes will be stored.
 * @return true if the read operation was successful, false otherwise.
 */
bool read_abs_bytes(BLOCK_DEVICE *disk, uint32_t byte_offset, uint32_t size, void *out)
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
bool read_block(EXT2_VOLUME *volume, uint32_t block, void *out)
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
bool read_group_desc(EXT2_VOLUME *volume, uint32_t group, EXT2_BLOCK_GROUP_DESC *out_desc)
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
 * writes bytes to the disk at the specified absolute byte offset and size from the provided input buffer.
 * @param disk pointer to the block device representing the disk.
 * @param byte_offset the absolute byte offset on the disk to start writing to.
 * @param size the number of bytes to write.
 * @param in pointer to the input buffer containing the bytes to be written.
 * @return true if the write operation was successful, false otherwise.
 */
bool write_abs_bytes(BLOCK_DEVICE *disk, uint32_t byte_offset, uint32_t size, const void *in)
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
bool write_block(EXT2_VOLUME *volume, uint32_t block, const void *in)
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
bool write_group_desc(EXT2_VOLUME *volume, uint32_t group, const EXT2_BLOCK_GROUP_DESC *desc)
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
bool write_superblock(EXT2_VOLUME *volume)
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
bool read_inode_bitmap(EXT2_VOLUME *volume, uint32_t group, uint8_t *bitmap)
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
bool write_inode_bitmap(EXT2_VOLUME *volume, uint32_t group, const uint8_t *bitmap)
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
bool read_block_bitmap(EXT2_VOLUME *volume, uint32_t group, uint8_t *bitmap)
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
bool write_block_bitmap(EXT2_VOLUME *volume, uint32_t group, const uint8_t *bitmap)
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

#include "ext2_internal.h"

/**
 * reads bytes from the disk at the specified absolute byte offset and size into the provided output buffer.
 * @param disk pointer to the block device representing the disk.
 * @param byte_offset the absolute byte offset on the disk to start reading from.
 * @param size the number of bytes to read.
 * @param out pointer to the output buffer where the read bytes will be stored.
 * @param sector_buf scratch buffer for partial-sector reads (must be EXT2_SECTOR_SIZE bytes).
 * @return true if the read operation was successful, false otherwise.
 */
bool read_abs_bytes(block_device_t *disk, uint32_t byte_offset, uint32_t size, void *out, uint8_t *sector_buf)
{
    uint8_t *dst = (uint8_t *)out;

    while (size > 0)
    {
        uint32_t lba = byte_offset / EXT2_SECTOR_SIZE;
        uint32_t offset = byte_offset % EXT2_SECTOR_SIZE;
        uint32_t chunk = EXT2_MIN(EXT2_SECTOR_SIZE - offset, size);

        if (!block_device_read(disk, lba, 1, sector_buf))
        {
            return false;
        }

        memcpy(dst, sector_buf + offset, chunk);

        dst += chunk;
        byte_offset += chunk;
        size -= chunk;
    }

    return true;
}

/**
 * reads a block from the EXT2 volume into the provided output buffer.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param block the block number to read from the volume.
 * @param out pointer to the output buffer where the read block will be stored.
 * @return true if the read operation was successful, false otherwise.
 */
bool read_block(ext2_volume_t *volume, uint32_t block, void *out)
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
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param group the block group number to read the descriptor for.
 * @param out_desc pointer to the ext2_block_group_desc_t structure where the read descriptor will be stored.
 * @return true if the read operation was successful, false otherwise.
 */
bool read_group_desc(ext2_volume_t *volume, uint32_t group, ext2_block_group_desc_t *out_desc)
{
    uint32_t bgdt_byte;
    uint32_t desc_offset;

    if (!volume || !out_desc)
    {
        return false;
    }

    bgdt_byte = volume->bgdt_start_block * volume->block_size;
    desc_offset = group * (uint32_t)sizeof(ext2_block_group_desc_t);
    return read_abs_bytes(volume->disk, bgdt_byte + desc_offset, (uint32_t)sizeof(ext2_block_group_desc_t), out_desc, volume->buf_sector);
}

/**
 * writes bytes to the disk at the specified absolute byte offset and size from the provided input buffer.
 * @param disk pointer to the block device representing the disk.
 * @param byte_offset the absolute byte offset on the disk to start writing to.
 * @param size the number of bytes to write.
 * @param in pointer to the input buffer containing the bytes to be written.
 * @param sector_buf scratch buffer for partial-sector read-modify-write (must be EXT2_SECTOR_SIZE bytes).
 * @return true if the write operation was successful, false otherwise.
 */
bool write_abs_bytes(block_device_t *disk, uint32_t byte_offset, uint32_t size, const void *in, uint8_t *sector_buf)
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
            if (!block_device_read(disk, lba, 1, sector_buf))
            {
                return false;
            }

            memcpy(sector_buf + offset, src, chunk);

            if (!block_device_write(disk, lba, 1, sector_buf))
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
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param block the block number to write to the volume.
 * @param in pointer to the input buffer containing the block data to be written.
 * @return true if the write operation was successful, false otherwise.
 */
bool write_block(ext2_volume_t *volume, uint32_t block, const void *in)
{
    if (!volume || !volume->disk || !in || volume->sectors_per_block == 0)
    {
        return false;
    }

    return block_device_write(volume->disk, block * volume->sectors_per_block, (uint8_t)volume->sectors_per_block, in);
}

/**
 * writes the block group descriptor for the specified group to the EXT2 volume from the provided input structure.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param group the block group number to write the descriptor for.
 * @param desc pointer to the ext2_block_group_desc_t structure containing the descriptor data to be written.
 * @return true if the write operation was successful, false otherwise.
 */
bool write_group_desc(ext2_volume_t *volume, uint32_t group, const ext2_block_group_desc_t *desc)
{
    uint32_t bgdt_byte;
    uint32_t desc_offset;

    if (!volume || !desc || group >= volume->block_group_count)
    {
        return false;
    }

    bgdt_byte = volume->bgdt_start_block * volume->block_size;
    desc_offset = group * (uint32_t)sizeof(ext2_block_group_desc_t);
    return write_abs_bytes(volume->disk, bgdt_byte + desc_offset, (uint32_t)sizeof(ext2_block_group_desc_t), desc, volume->buf_sector);
}

/**
 * writes the superblock to the EXT2 volume.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @return true if the write operation was successful, false otherwise.
 */
bool write_superblock(ext2_volume_t *volume)
{
    if (!volume)
    {
        return false;
    }

    return write_abs_bytes(volume->disk, EXT2_SUPERBLOCK_OFFSET, (uint32_t)sizeof(ext2_superblock_t), &volume->superblock, volume->buf_sector);
}

/**
 * reads the inode bitmap for the specified block group from the EXT2 volume into the provided output buffer.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param group the block group number to read the inode bitmap for.
 * @param bitmap pointer to the output buffer where the read inode bitmap will be stored.
 * @return true if the read operation was successful, false otherwise.
 */
bool read_inode_bitmap(ext2_volume_t *volume, uint32_t group, uint8_t *bitmap)
{
    ext2_block_group_desc_t desc;

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
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param group the block group number to write the inode bitmap for.
 * @param bitmap pointer to the input buffer containing the inode bitmap data to be written.
 * @return true if the write operation was successful, false otherwise.
 */
bool write_inode_bitmap(ext2_volume_t *volume, uint32_t group, const uint8_t *bitmap)
{
    ext2_block_group_desc_t desc;

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
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param group the block group number to read the block bitmap for.
 * @param bitmap pointer to the output buffer where the read block bitmap will be stored.
 * @return true if the read operation was successful, false otherwise.
 */
bool read_block_bitmap(ext2_volume_t *volume, uint32_t group, uint8_t *bitmap)
{
    ext2_block_group_desc_t desc;

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
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param group the block group number to write the block bitmap for.
 * @param bitmap pointer to the input buffer containing the block bitmap data to be written.
 * @return true if the write operation was successful, false otherwise.
 */
bool write_block_bitmap(ext2_volume_t *volume, uint32_t group, const uint8_t *bitmap)
{
    ext2_block_group_desc_t desc;

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

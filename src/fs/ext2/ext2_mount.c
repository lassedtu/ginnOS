#include "ext2_internal.h"

kerr_t ext2_initialize(ext2_volume_t *volume, block_device_t *disk)
{
    ext2_superblock_t sb;
    uint32_t unsupported;

    if (!volume || !disk)
    {
        return KERR_INVAL;
    }

    if (!read_abs_bytes(disk, EXT2_SUPERBLOCK_OFFSET, (uint32_t)sizeof(ext2_superblock_t), &sb))
    {
        return KERR_IO;
    }

    if (sb.s_magic != EXT2_SUPERBLOCK_MAGIC)
    {
        return KERR_INVAL;
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
        return KERR_INVAL;
    }

    if ((volume->block_size % EXT2_SECTOR_SIZE) != 0)
    {
        return KERR_INVAL;
    }

    volume->sectors_per_block = volume->block_size / EXT2_SECTOR_SIZE;

    if (volume->inode_size > EXT2_MAX_INODE_SIZE || volume->inode_size < 128u)
    {
        return KERR_INVAL;
    }

    if (volume->blocks_per_group == 0 || volume->inodes_per_group == 0)
    {
        return KERR_INVAL;
    }

    unsupported = sb.s_feature_incompat & ~(EXT2_FEATURE_INCOMPAT_FILETYPE);
    if (unsupported != 0)
    {
        return KERR_INVAL;
    }

    volume->block_group_count = (sb.s_blocks_count - sb.s_first_data_block + sb.s_blocks_per_group - 1u) / sb.s_blocks_per_group;
    if (volume->block_group_count == 0)
    {
        return KERR_INVAL;
    }

    return KERR_OK;
}

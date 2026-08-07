#include "partition.h"

#define EXT2_SUPERBLOCK_MAGIC 0xEF53u

/**
 * read blocks from a partition.
 */
static bool partition_block_read(BLOCK_DEVICE *device, uint32_t startBlock, uint8_t blockCount, void *dest)
{
    PARTITION_DEVICE *part;
    if (!device || !device->context)
    {
        return false;
    }

    part = (PARTITION_DEVICE *)device->context;
    if (!part->parent)
    {
        return false;
    }

    return block_device_read(part->parent, part->start_lba + startBlock, blockCount, dest);
}

/**
 * write blocks to a partition.
 */
static bool partition_block_write(BLOCK_DEVICE *device, uint32_t startBlock, uint8_t blockCount, const void *src)
{
    PARTITION_DEVICE *part;
    if (!device || !device->context)
    {
        return false;
    }

    part = (PARTITION_DEVICE *)device->context;
    if (!part->parent)
    {
        return false;
    }

    return block_device_write(part->parent, part->start_lba + startBlock, blockCount, src);
}

bool PARTITION_Initialize(PARTITION_DEVICE *part, BLOCK_DEVICE *parent, uint32_t start_lba)
{
    if (!part || !parent)
    {
        return false;
    }

    part->parent = parent;
    part->start_lba = start_lba;
    part->block.bytes_per_block = parent->bytes_per_block;
    part->block.context = part;
    part->block.read_blocks = partition_block_read;
    part->block.write_blocks = partition_block_write;
    return true;
}

/**
 * check if an EXT2 superblock exists at the given LBA on the parent block device.
 */
static bool check_ext2_at_lba(BLOCK_DEVICE *parent, uint32_t lba)
{
    uint8_t buf[512];
    uint16_t magic;

    /* EXT2 superblock is at byte offset 1024 of partition.
     * with 512-byte sectors, byte 1024 is sector lba + 2. */
    if (!block_device_read(parent, lba + 2u, 1, buf))
    {
        return false;
    }

    magic = (uint16_t)buf[56] | ((uint16_t)buf[57] << 8);
    return magic == EXT2_SUPERBLOCK_MAGIC;
}

bool PARTITION_DetectExt2(PARTITION_DEVICE *part, BLOCK_DEVICE *parent)
{
    uint8_t sector0[512];
    uint32_t mbr_lba;

    if (!part || !parent)
    {
        return false;
    }

    // check MBR partition table at sector 0
    if (block_device_read(parent, 0, 1, sector0))
    {
        mbr_lba = (uint32_t)sector0[0x1BE + 8] |
                  ((uint32_t)sector0[0x1BE + 9] << 8) |
                  ((uint32_t)sector0[0x1BE + 10] << 16) |
                  ((uint32_t)sector0[0x1BE + 11] << 24);

        if (mbr_lba > 0 && check_ext2_at_lba(parent, mbr_lba))
        {
            return PARTITION_Initialize(part, parent, mbr_lba);
        }
    }

    // check candidate LBAs: 63 (standard MBR offset), 2048 (1MB alignment), 0 (raw)
    if (check_ext2_at_lba(parent, 63u))
    {
        return PARTITION_Initialize(part, parent, 63u);
    }

    if (check_ext2_at_lba(parent, 2048u))
    {
        return PARTITION_Initialize(part, parent, 2048u);
    }

    if (check_ext2_at_lba(parent, 0u))
    {
        return PARTITION_Initialize(part, parent, 0u);
    }

    // fallback: initialize at offset 0
    return PARTITION_Initialize(part, parent, 0u);
}

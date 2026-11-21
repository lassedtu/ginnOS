#include "partition.h"

#include "common/memory.h"

#define EXT2_SUPERBLOCK_MAGIC 0xEF53u

/**
 * read blocks from a partition.
 */
static bool partition_block_read(block_device_t *device, uint32_t startBlock, uint8_t blockCount,
                                 void *dest)
{
    partition_device_t *part;
    if (!device || !device->context)
    {
        return false;
    }

    part = (partition_device_t *)device->context;
    if (!part->parent)
    {
        return false;
    }

    return block_device_read(part->parent, part->start_lba + startBlock, blockCount, dest);
}

/**
 * write blocks to a partition.
 */
static bool partition_block_write(block_device_t *device, uint32_t startBlock, uint8_t blockCount,
                                  const void *src)
{
    partition_device_t *part;
    if (!device || !device->context)
    {
        return false;
    }

    part = (partition_device_t *)device->context;
    if (!part->parent)
    {
        return false;
    }

    return block_device_write(part->parent, part->start_lba + startBlock, blockCount, src);
}

/**
 * flush the partition by flushing its parent device.
 */
static bool partition_block_flush(block_device_t *device)
{
    partition_device_t *part;
    if (!device || !device->context)
    {
        return false;
    }

    part = (partition_device_t *)device->context;
    if (!part->parent)
    {
        return false;
    }

    return block_device_flush(part->parent);
}

/**
 * trim a range on the partition, translated into the parent's LBA space.
 */
static bool partition_block_trim(block_device_t *device, uint32_t startBlock, uint32_t blockCount)
{
    partition_device_t *part;
    if (!device || !device->context)
    {
        return false;
    }

    part = (partition_device_t *)device->context;
    if (!part->parent)
    {
        return false;
    }

    return block_device_trim(part->parent, part->start_lba + startBlock, blockCount);
}

bool partition_initialize(partition_device_t *part, block_device_t *parent, uint32_t start_lba)
{
    if (!part || !parent)
    {
        return false;
    }

    part->parent = parent;
    part->start_lba = start_lba;
    part->block.bytes_per_block = parent->bytes_per_block;
    // best-effort size: whatever of the parent lies past our start. the exact
    // partition length isn't parsed here, so this is an upper bound.
    part->block.total_blocks = (parent->total_blocks > start_lba)
                                   ? (parent->total_blocks - start_lba)
                                   : 0;
    part->block.context = part;
    part->block.read_blocks = partition_block_read;
    part->block.write_blocks = partition_block_write;
    // forward cache/trim hints to the parent so callers can treat a partition
    // like any other block device.
    part->block.flush = partition_block_flush;
    part->block.trim = partition_block_trim;
    return true;
}

/**
 * check if an EXT2 superblock exists at the given LBA on the parent block device.
 */
static bool check_ext2_at_lba(block_device_t *parent, uint32_t lba)
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

bool partition_detect_ext2(partition_device_t *part, block_device_t *parent)
{
    uint8_t sector0[512];
    uint32_t mbr_lba;

    if (!part || !parent)
    {
        return false;
    }

    // look for ext2 in each of the four primary MBR partitions. a limine
    // disk has the FAT boot partition first and ext2 second.
    if (block_device_read(parent, 0, 1, sector0) && sector0[510] == 0x55 &&
        sector0[511] == 0xAA)
    {
        for (uint32_t i = 0; i < 4; i++)
        {
            const uint8_t *entry = &sector0[0x1BE + i * 16];
            mbr_lba = (uint32_t)entry[8] | ((uint32_t)entry[9] << 8) |
                      ((uint32_t)entry[10] << 16) | ((uint32_t)entry[11] << 24);

            if (entry[4] != 0 && mbr_lba > 0 && check_ext2_at_lba(parent, mbr_lba))
            {
                return partition_initialize(part, parent, mbr_lba);
            }
        }
    }

    // check candidate LBAs: 63 (standard MBR offset), 2048 (1MB alignment), 0 (raw)
    if (check_ext2_at_lba(parent, 63u))
    {
        return partition_initialize(part, parent, 63u);
    }

    if (check_ext2_at_lba(parent, 2048u))
    {
        return partition_initialize(part, parent, 2048u);
    }

    if (check_ext2_at_lba(parent, 0u))
    {
        return partition_initialize(part, parent, 0u);
    }

    // fallback: initialize at offset 0
    return partition_initialize(part, parent, 0u);
}

bool partition_is_ginnos_root(block_device_t *device)
{
    static const char label[] = GINNOS_ROOT_LABEL;
    uint8_t buf[512];

    // the superblock is at byte 1024: sector 2. the label is at offset 120
    // (16 bytes, padded with zeros).
    if (!device || !block_device_read(device, 2u, 1, buf))
    {
        return false;
    }

    uint16_t magic = (uint16_t)buf[56] | ((uint16_t)buf[57] << 8);
    if (magic != EXT2_SUPERBLOCK_MAGIC)
    {
        return false;
    }

    for (uint32_t i = 0; i < 16u; i++)
    {
        char expected = i < sizeof(label) - 1 ? label[i] : '\0';
        if ((char)buf[120 + i] != expected)
        {
            return false;
        }
    }
    return true;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/**
 * set up part at lba and check it for the ginnOS root label.
 */
static bool try_root_at(partition_device_t *part, block_device_t *parent, uint32_t lba)
{
    return partition_initialize(part, parent, lba) && partition_is_ginnos_root(&part->block);
}

/**
 * the GPT partitions (UEFI specification, chapter 5.3): the header at LBA 1
 * gives where the partition entries are, how many, and how big each is.
 * an entry with a zero type GUID is unused. partitions that start above
 * 2^32 sectors (2 TiB) cannot be used with 32-bit block numbers.
 */
static bool find_in_gpt(partition_device_t *part, block_device_t *parent)
{
    uint8_t header[512];
    if (!block_device_read(parent, 1, 1, header) || memcmp(header, "EFI PART", 8) != 0)
    {
        return false;
    }

    uint32_t entries_lba = le32(header + 72); // the high half must be 0
    uint32_t entry_count = le32(header + 80);
    uint32_t entry_size = le32(header + 84);
    if (le32(header + 76) != 0 || entry_size < 128 || entry_size > 512 ||
        512 % entry_size != 0 || entry_count > 1024)
    {
        return false;
    }

    uint8_t sector[512];
    uint32_t per_sector = 512 / entry_size;
    for (uint32_t i = 0; i < entry_count; i++)
    {
        if (i % per_sector == 0 &&
            !block_device_read(parent, entries_lba + i / per_sector, 1, sector))
        {
            return false;
        }

        const uint8_t *entry = sector + (i % per_sector) * entry_size;
        bool used = false;
        for (int b = 0; b < 16; b++)
        {
            used = used || entry[b] != 0;
        }
        uint32_t first = le32(entry + 32);
        if (used && le32(entry + 36) == 0 && first != 0 && try_root_at(part, parent, first))
        {
            return true;
        }
    }
    return false;
}

bool partition_find_ginnos_root(partition_device_t *part, block_device_t *parent)
{
    uint8_t mbr[512];
    if (!part || !parent || !block_device_read(parent, 0, 1, mbr))
    {
        return false;
    }

    if (mbr[510] == 0x55 && mbr[511] == 0xAA)
    {
        for (uint32_t i = 0; i < 4; i++)
        {
            const uint8_t *entry = &mbr[0x1BE + i * 16];
            if (entry[4] == 0xEE)
            {
                return find_in_gpt(part, parent);
            }
        }
        for (uint32_t i = 0; i < 4; i++)
        {
            const uint8_t *entry = &mbr[0x1BE + i * 16];
            uint32_t lba = le32(entry + 8);
            if (entry[4] != 0 && lba != 0 && try_root_at(part, parent, lba))
            {
                return true;
            }
        }
    }

    // the stage2 disk layout: ext2 at sector 63; or a disk with no table
    return try_root_at(part, parent, 63u) || try_root_at(part, parent, 0u);
}

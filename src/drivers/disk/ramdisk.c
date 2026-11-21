#include "ramdisk.h"

#include "common/memory.h"

#define RAMDISK_BLOCK_SIZE 512u

/**
 * the byte offset of a block range, or false if the range is not on the
 * disk.
 */
static bool block_range(const ramdisk_t *disk, uint32_t start, uint32_t count, uint32_t *offset)
{
    uint32_t total = disk->block.total_blocks;
    if (start >= total || count > total - start)
    {
        return false;
    }
    *offset = start * RAMDISK_BLOCK_SIZE;
    return true;
}

static bool ramdisk_read(block_device_t *device, uint32_t start, uint8_t count, void *dest)
{
    const ramdisk_t *disk = device->context;
    uint32_t offset;
    if (!block_range(disk, start, count, &offset))
    {
        return false;
    }
    memcpy(dest, disk->data + offset, (size_t)count * RAMDISK_BLOCK_SIZE);
    return true;
}

static bool ramdisk_write(block_device_t *device, uint32_t start, uint8_t count, const void *src)
{
    ramdisk_t *disk = device->context;
    uint32_t offset;
    if (!block_range(disk, start, count, &offset))
    {
        return false;
    }
    memcpy(disk->data + offset, src, (size_t)count * RAMDISK_BLOCK_SIZE);
    return true;
}

bool ramdisk_initialize(ramdisk_t *disk, void *data, uint32_t size)
{
    if (!disk || !data || size < RAMDISK_BLOCK_SIZE)
    {
        return false;
    }

    memset(disk, 0, sizeof(*disk));
    disk->data = data;
    disk->size = size;
    disk->block.bytes_per_block = RAMDISK_BLOCK_SIZE;
    disk->block.total_blocks = size / RAMDISK_BLOCK_SIZE;
    disk->block.context = disk;
    disk->block.read_blocks = ramdisk_read;
    disk->block.write_blocks = ramdisk_write;
    return true;
}

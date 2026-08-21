#include "block_device.h"

bool block_device_read(block_device_t *device, uint32_t startBlock, uint8_t blockCount, void *dest)
{
    if (!device || !device->read_blocks || !dest || blockCount == 0)
    {
        return false;
    }

    return device->read_blocks(device, startBlock, blockCount, dest);
}

bool block_device_write(block_device_t *device, uint32_t startBlock, uint8_t blockCount, const void *src)
{
    if (!device || !device->write_blocks || !src || blockCount == 0)
    {
        return false;
    }

    return device->write_blocks(device, startBlock, blockCount, src);
}

#include "block_device.h"

bool block_device_read(block_device_t *device, uint32_t startBlock, uint8_t blockCount, void *dest)
{
    if (!device || !device->read_blocks || !dest || blockCount == 0)
    {
        return false;
    }

    return device->read_blocks(device, startBlock, blockCount, dest);
}

bool block_device_write(block_device_t *device, uint32_t startBlock, uint8_t blockCount,
                        const void *src)
{
    if (!device || !device->write_blocks || !src || blockCount == 0)
    {
        return false;
    }

    return device->write_blocks(device, startBlock, blockCount, src);
}

bool block_device_flush(block_device_t *device)
{
    if (!device)
    {
        return false;
    }

    // no flush op means the device has no write-back cache; nothing to do.
    if (!device->flush)
    {
        return true;
    }

    return device->flush(device);
}

bool block_device_trim(block_device_t *device, uint32_t startBlock, uint32_t blockCount)
{
    if (!device || blockCount == 0)
    {
        return false;
    }

    // trim is an optional hint; unsupported devices simply ignore it.
    if (!device->trim)
    {
        return true;
    }

    return device->trim(device, startBlock, blockCount);
}

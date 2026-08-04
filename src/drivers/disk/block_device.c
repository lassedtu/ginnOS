#include "block_device.h"

bool block_device_read(BLOCK_DEVICE *device, uint32_t startBlock, uint8_t blockCount, void *dest)
{
    if (!device || !device->read_blocks || !dest || blockCount == 0)
    {
        return false;
    }

    return device->read_blocks(device, startBlock, blockCount, dest);
}

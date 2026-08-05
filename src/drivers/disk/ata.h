#pragma once

#include "block_device.h"

/**
 * ATA device structure representing an ATA disk drive.
 */
typedef struct
{
    BLOCK_DEVICE block; // partition block device wrapper.
} ATA_DEVICE;

/**
 * initialize an ATA device for reading blocks.
 * @param device ATA device object to initialize.
 * @return true on success, false on failure.
 */
bool ATA_Initialize(ATA_DEVICE *device);

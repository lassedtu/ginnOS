#pragma once

#include "block_device.h"

typedef struct
{
    BLOCK_DEVICE block;
} ATA_DEVICE;

bool ATA_Initialize(ATA_DEVICE *device);

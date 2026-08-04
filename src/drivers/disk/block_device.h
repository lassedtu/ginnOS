#pragma once

#include "../../common/stdint.h"

typedef struct BLOCK_DEVICE BLOCK_DEVICE;

typedef bool (*BlockReadFn)(BLOCK_DEVICE *device, uint32_t startBlock, uint8_t blockCount, void *dest);

struct BLOCK_DEVICE
{
    uint16_t bytes_per_block;
    void *context;
    BlockReadFn read_blocks;
};

bool block_device_read(BLOCK_DEVICE *device, uint32_t startBlock, uint8_t blockCount, void *dest);

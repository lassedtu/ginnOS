#pragma once

#include "../../common/stdint.h"

typedef struct BLOCK_DEVICE BLOCK_DEVICE;

typedef bool (*BlockReadFn)(BLOCK_DEVICE *device, uint32_t startBlock, uint8_t blockCount, void *dest);

/**
 * block device abstraction for reading blocks from a storage device.
 */
struct BLOCK_DEVICE
{
    uint16_t bytes_per_block; // number of bytes in each block (sector) of the device.
    void *context;            // pointer to device-specific context data (e.g., ATA device structure).
    BlockReadFn read_blocks;  // function pointer to the block read function for the device.
};

/**
 * read blocks from a block device.
 * @param device initialized block device backend.
 * @param startBlock starting block number to read from.
 * @param blockCount number of blocks to read.
 * @param dest destination buffer to store the read data.
 * @return true on success. false on failure.
 */
bool block_device_read(BLOCK_DEVICE *device, uint32_t startBlock, uint8_t blockCount, void *dest);

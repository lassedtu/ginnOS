#pragma once

#include "common/stdint.h"

typedef struct block_device block_device_t;

typedef bool (*BlockReadFn)(block_device_t *device, uint32_t startBlock, uint8_t blockCount, void *dest);
typedef bool (*BlockWriteFn)(block_device_t *device, uint32_t startBlock, uint8_t blockCount, const void *src);

/**
 * block device abstraction for reading blocks from a storage device.
 */
struct block_device
{
    uint16_t bytes_per_block;  // number of bytes in each block (sector) of the device.
    void *context;             // pointer to device-specific context data (e.g., ATA device structure).
    BlockReadFn read_blocks;   // function pointer to the block read function for the device.
    BlockWriteFn write_blocks; // function pointer to the block write function for the device.
};

/**
 * read blocks from a block device.
 * @param device initialized block device backend.
 * @param startBlock starting block number to read from.
 * @param blockCount number of blocks to read.
 * @param dest destination buffer to store the read data.
 * @return true on success. false on failure.
 */
bool block_device_read(block_device_t *device, uint32_t startBlock, uint8_t blockCount, void *dest);

/**
 * write blocks to a block device.
 * @param device initialized block device backend.
 * @param startBlock starting block number to write to.
 * @param blockCount number of blocks to write.
 * @param src source buffer containing the data to write.
 * @return true on success. false on failure.
 */
bool block_device_write(block_device_t *device, uint32_t startBlock, uint8_t blockCount, const void *src);

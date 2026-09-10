#pragma once

#include "common/stdint.h"

typedef struct block_device block_device_t;

typedef bool (*BlockReadFn)(block_device_t *device, uint32_t startBlock, uint8_t blockCount,
                            void *dest);
typedef bool (*BlockWriteFn)(block_device_t *device, uint32_t startBlock, uint8_t blockCount,
                             const void *src);
typedef bool (*BlockFlushFn)(block_device_t *device);
typedef bool (*BlockTrimFn)(block_device_t *device, uint32_t startBlock, uint32_t blockCount);

/**
 * block device abstraction for reading blocks from a storage device.
 */
struct block_device
{
    uint16_t bytes_per_block; // number of bytes in each block (sector) of the device.
    uint32_t total_blocks;    // total addressable blocks on the device (0 if unknown).
    void *context; // pointer to device-specific context data (e.g., ATA device structure).
    BlockReadFn read_blocks;   // function pointer to the block read function for the device.
    BlockWriteFn write_blocks; // function pointer to the block write function for the device.
    BlockFlushFn flush;        // optional: push cached writes to media, or NULL if not cached.
    BlockTrimFn trim;          // optional: hint that blocks are unused (SSD/virtual), or NULL.
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
bool block_device_write(block_device_t *device, uint32_t startBlock, uint8_t blockCount,
                        const void *src);

/**
 * flush any cached writes to the underlying media.
 * devices without a write-back cache leave the flush op NULL, this wrapper
 * treats that as a successful no-op so callers can flush unconditionally.
 * @param device initialized block device backend.
 * @return true on success (including "nothing to flush"), false on I/O error.
 */
bool block_device_flush(block_device_t *device);

/**
 * hint that a range of blocks is no longer in use (TRIM/discard).
 * this is only an optimization for SSDs and virtual disks, devices that do
 * not support it leave the trim op NULL and this wrapper is a no-op success.
 * @param device initialized block device backend.
 * @param startBlock first block in the range.
 * @param blockCount number of blocks in the range.
 * @return true on success (including "not supported"), false on error.
 */
bool block_device_trim(block_device_t *device, uint32_t startBlock, uint32_t blockCount);

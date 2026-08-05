#pragma once

#include "block_device.h"

/**
 * partition device structure representing a partition on a block device.
 */
typedef struct
{
    BLOCK_DEVICE block;   // partition block device wrapper.
    BLOCK_DEVICE *parent; // parent block device backend.
    uint32_t start_lba;   // starting LBA sector of partition.
} PARTITION_DEVICE;

/**
 * initialize a partition wrapper device around a parent block device.
 * @param part partition device object to initialize.
 * @param parent parent block device backend.
 * @param start_lba starting LBA sector of partition.
 * @return true on success, false on failure.
 */
bool PARTITION_Initialize(PARTITION_DEVICE *part, BLOCK_DEVICE *parent, uint32_t start_lba);

/**
 * detect an EXT2 partition on parent block device and initialize partition wrapper.
 * checks MBR partition table and candidate LBAs (63, 2048, 0).
 * @param part partition device object to initialize.
 * @param parent parent block device backend.
 * @return true if an EXT2 partition was found and initialized, false otherwise.
 */
bool PARTITION_DetectExt2(PARTITION_DEVICE *part, BLOCK_DEVICE *parent);

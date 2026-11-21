#pragma once

#include "block_device.h"

/**
 * partition device structure representing a partition on a block device.
 */
typedef struct
{
    block_device_t block;   // partition block device wrapper.
    block_device_t *parent; // parent block device backend.
    uint32_t start_lba;     // starting LBA sector of partition.
} partition_device_t;

/**
 * initialize a partition wrapper device around a parent block device.
 * @param part partition device object to initialize.
 * @param parent parent block device backend.
 * @param start_lba starting LBA sector of partition.
 * @return true on success, false on failure.
 */
bool partition_initialize(partition_device_t *part, block_device_t *parent, uint32_t start_lba);

/**
 * detect an EXT2 partition on parent block device and initialize partition wrapper.
 * checks MBR partition table and candidate LBAs (63, 2048, 0).
 * @param part partition device object to initialize.
 * @param parent parent block device backend.
 * @return true if an EXT2 partition was found and initialized, false otherwise.
 */
bool partition_detect_ext2(partition_device_t *part, block_device_t *parent);

/* the volume label that tools/ext2/make_image.py gives a ginnOS root */
#define GINNOS_ROOT_LABEL "GINNOS_EXT2"

/**
 * check that an ext2 filesystem on a block device has the ginnOS root label.
 * the kernel uses this to not mount a disk of another operating system on
 * real hardware.
 * @param device the block device (a partition) with the filesystem.
 * @return true if the superblock has the ext2 magic and GINNOS_ROOT_LABEL.
 */
bool partition_is_ginnos_root(block_device_t *device);

#pragma once

/**
 * @file ramdisk.h
 * @brief a block device on a range of memory.
 *
 * the root filesystem image that the bootloader loads as a module becomes a
 * block device with this driver. writes change only the memory, so they are
 * lost at the next boot.
 */

#include "block_device.h"
#include "common/stdint.h"

typedef struct
{
    block_device_t block; // the block device interface (context points here).
    uint8_t *data;        // the first byte of the image.
    uint32_t size;        // the size of the image in bytes.

} ramdisk_t;

/**
 * make a block device of 512-byte blocks on memory.
 * @param disk the ramdisk to set up.
 * @param data the first byte of the image (a kernel pointer).
 * @param size the size of the image in bytes. a part block at the end is
 *        not used.
 * @return false if the image is smaller than one block.
 */
bool ramdisk_initialize(ramdisk_t *disk, void *data, uint32_t size);

#pragma once

#include "block_device.h"

/**
 * ATA channel selection.
 * each channel has its own I/O base and control port.
 */
typedef enum
{
    ATA_CHANNEL_PRIMARY = 0,   // primary channel:   I/O base 0x1F0, control 0x3F6
    ATA_CHANNEL_SECONDARY = 1, // secondary channel: I/O base 0x170, control 0x376
} ata_channel_t;

/**
 * ATA drive selection within a channel.
 */
typedef enum
{
    ATA_DRIVE_MASTER = 0, // drive select byte 0xE0 (LBA mode, master)
    ATA_DRIVE_SLAVE = 1,  // drive select byte 0xF0 (LBA mode, slave)
} ata_drive_t;

/**
 * ATA device structure.
 * carries per-device I/O port state so multiple devices on different
 * channels or drive positions can coexist without hardcoded port macros.
 */
typedef struct
{
    block_device_t block;  // block device interface, must be first for safe casting.
    uint16_t io_base;      // I/O base port (0x1F0 primary, 0x170 secondary).
    uint16_t control_base; // control/alt-status port (0x3F6 primary, 0x376 secondary).
    uint8_t drive_select;  // drive-select byte written to the drive/head register.
    uint64_t sector_count; // total addressable sectors reported by IDENTIFY DEVICE.
} ata_device_t;

/**
 * initialize an ATA device on the specified channel and drive position.
 * issues IDENTIFY DEVICE to confirm the drive is present and is not ATAPI.
 * populates all port fields and wires up the block device interface.
 *
 * @param device   ATA device object to initialize.
 * @param channel  ATA_CHANNEL_PRIMARY or ATA_CHANNEL_SECONDARY.
 * @param drive    ATA_DRIVE_MASTER or ATA_DRIVE_SLAVE.
 * @return true on success, false if no drive responds or device is ATAPI.
 */
bool ata_initialize(ata_device_t *device, ata_channel_t channel, ata_drive_t drive);

#pragma once

/**
 * @file nvme.h
 * @brief NVMe disks (PCI class 01:08:02).
 *
 * nvme_init sets up the first NVMe controller with one admin queue pair and
 * one I/O queue pair, and makes namespace 1 a block device of 512-byte
 * sectors. the driver waits for each command by polling the completion
 * queue (interrupts stay off for now). namespaces with another sector size
 * are not used.
 */

#include "block_device.h"
#include "common/stdint.h"
#include "kernel/memory/phys.h"

typedef struct
{
    block_device_t block; // the block device interface (context points here)
    uint64_t sectors;     // the namespace size in 512-byte sectors
} nvme_disk_t;

/**
 * find and set up the first NVMe controller. call after pci_init and
 * mmio_init.
 * @return the disk, or NULL if there is none or it cannot be used.
 */
nvme_disk_t *nvme_init(void);

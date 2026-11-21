#pragma once

/**
 * @file nvme.h
 * @brief NVMe disks (PCI class 01:08:02).
 *
 * nvme_init sets up the first NVMe controller with one admin queue pair and
 * one I/O queue pair, and makes namespace 1 a block device of 512-byte
 * sectors. a process that waits for a command sleeps until the controller
 * interrupts; during boot, or without an interrupt, the driver polls the
 * completion queue. namespaces with another sector size are not used.
 */

#include "block_device.h"
#include "common/stdint.h"
#include "kernel/memory/phys.h"
#include "kernel/sync/completion.h"
#include "kernel/sync/mutex.h"

typedef struct
{
    block_device_t block; // the block device interface (context points here)
    uint64_t sectors;     // the namespace size in 512-byte sectors
    mutex_t lock;         // one command at a time
    completion_t done;    // the completion queue interrupt
} nvme_disk_t;

/**
 * find and set up the first NVMe controller. call after pci_init and
 * mmio_init.
 * @return the disk, or NULL if there is none or it cannot be used.
 */
nvme_disk_t *nvme_init(void);

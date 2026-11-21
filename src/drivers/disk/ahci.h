#pragma once

/**
 * @file ahci.h
 * @brief SATA disks through an AHCI controller (PCI class 01:06:01).
 *
 * ahci_init finds the controller, maps its registers (BAR 5) and sets up
 * each port that has a SATA disk. each disk is a block_device_t of 512-byte
 * sectors. the transfers use DMA with 48-bit LBA commands. a process that
 * waits for a command sleeps until the port interrupts; during boot, or
 * without an interrupt, the driver polls the port.
 */

#include "block_device.h"
#include "common/stdint.h"
#include "kernel/memory/phys.h"
#include "kernel/sync/completion.h"
#include "kernel/sync/mutex.h"

#define AHCI_MAX_DISKS 4

typedef struct
{
    block_device_t block; // the block device interface (context points here)
    volatile uint32_t *port; // this port's registers
    uint8_t port_number;
    uint8_t *dma_page;      // command list, FIS area, command table, bounce buffer
    phys_addr_t dma_phys;   // the physical address of dma_page
    uint64_t sectors;       // from IDENTIFY DEVICE
    mutex_t lock;           // one command at a time
    completion_t done;      // the port interrupt
    volatile uint32_t irq_status; // the port status the interrupt handler cleared
} ahci_disk_t;

/**
 * find the AHCI controller (the first one) and its disks. call after
 * pci_init and mmio_init.
 * @return the number of disks found.
 */
uint32_t ahci_init(void);

/**
 * a disk by index (0 to the number ahci_init returned).
 * @return the disk, or NULL.
 */
ahci_disk_t *ahci_disk(uint32_t index);

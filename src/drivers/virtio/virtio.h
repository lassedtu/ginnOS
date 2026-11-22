#pragma once

/**
 * @file virtio.h
 * @brief virtio 1.0 devices on PCI (the "modern" interface).
 *
 * the device's registers are in BARs that vendor capabilities point at:
 * the common configuration, the notify area, the interrupt status and the
 * device configuration. queues are split virtqueues in one page each.
 *
 * requests are synchronous: virtq_call puts one request and one response
 * buffer on a queue, tells the device and waits (polling) for the answer.
 * that is enough for devices that are asked one thing at a time, such as
 * the control queue of virtio-gpu.
 */

#include "common/stdint.h"
#include "drivers/pci/pci.h"
#include "kernel/sync/spinlock.h"

#define VIRTIO_VENDOR_ID 0x1AF4

// device status bits
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED      128

// feature bits (the high word is feature select 1)
#define VIRTIO_F_VERSION_1 32

#define VIRTQ_SIZE 64 // entries per queue (the device may allow more)

typedef struct
{
    pci_device_t *pci;
    volatile uint8_t *common; // struct virtio_pci_common_cfg
    volatile uint8_t *notify; // the notify area
    uint32_t notify_multiplier;
    volatile uint8_t *isr;    // read to clear the interrupt
    volatile uint8_t *device; // the device's own configuration
} virtio_device_t;

typedef struct
{
    virtio_device_t *dev;
    uint16_t index;          // the queue's number
    uint16_t size;           // entries
    uint8_t *ring;           // descriptors, then the available ring, then the used ring
    uint8_t *buffer;         // a page for requests and responses
    volatile uint16_t *note; // where to write to tell the device
    uint16_t used_seen;      // the used ring index the driver has seen
    uint16_t avail_next;     // the available ring index of the next request
    spinlock_t lock;
} virtq_t;

/**
 * find the registers, reset the device and agree on features. the device
 * stays in that state until virtio_ready, so the driver can set up its
 * queues first.
 * @param features the bits the driver wants; VIRTIO_F_VERSION_1 is added.
 * @return false if this is not a modern virtio device or it refuses.
 */
bool virtio_init(virtio_device_t *dev, pci_device_t *pci, uint64_t features);

/**
 * set up a queue (before virtio_ready).
 * @return false if the device has no such queue or there is no memory.
 */
bool virtio_queue_init(virtio_device_t *dev, virtq_t *q, uint16_t index);

/**
 * tell the device the driver is ready.
 */
void virtio_ready(virtio_device_t *dev);

/**
 * route configuration change interrupts to MSI-X vector 0 (when the
 * device uses MSI-X) and no queue interrupts: queues are polled.
 */
void virtio_use_config_vector(virtio_device_t *dev);

/**
 * send a request and wait for the response.
 * @param request request_size bytes the device reads.
 * @param response response_size bytes the device writes.
 * @return false if the device did not answer in time. sizes up to half a
 *         page each.
 */
bool virtq_call(virtq_t *q, const void *request, uint32_t request_size, void *response,
                uint32_t response_size);

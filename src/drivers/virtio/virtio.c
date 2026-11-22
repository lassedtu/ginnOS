/**
 * @file virtio.c
 * @brief virtio 1.0 on PCI: registers, features, split virtqueues (see
 *        virtio.h).
 */

#include "virtio.h"

#include "common/memory.h"
#include "kernel/memory/mmio.h"
#include "kernel/memory/page.h"
#include "kernel/memory/phys.h"
#include "kernel/memory/pmm.h"

#define CAP_VENDOR 0x09

// virtio_pci_cap.cfg_type
#define CFG_COMMON 1
#define CFG_NOTIFY 2
#define CFG_ISR    3
#define CFG_DEVICE 4

// offsets in the common configuration
#define DEVICE_FEATURE_SELECT 0
#define DEVICE_FEATURE        4
#define DRIVER_FEATURE_SELECT 8
#define DRIVER_FEATURE        12
#define MSIX_CONFIG           16
#define DEVICE_STATUS         20
#define QUEUE_SELECT          22
#define QUEUE_SIZE            24
#define QUEUE_MSIX_VECTOR     26
#define QUEUE_ENABLE          28
#define QUEUE_NOTIFY_OFF      30
#define QUEUE_DESC            32
#define QUEUE_DRIVER          40
#define QUEUE_DEVICE          48

#define NO_VECTOR 0xFFFF

// descriptor flags
#define DESC_NEXT  1
#define DESC_WRITE 2 // the device writes this buffer

#define WAIT_LOOPS 50000000u // a second or so of polling

typedef struct
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) desc_t;

// where the parts of a queue are in its page
#define AVAIL_OFFSET (VIRTQ_SIZE * sizeof(desc_t))
#define USED_OFFSET  2048

static uint8_t read8(volatile uint8_t *base, uint32_t offset)
{
    return mmio_read8(base + offset);
}

static uint16_t read16(volatile uint8_t *base, uint32_t offset)
{
    return mmio_read16(base + offset);
}

static void write8(volatile uint8_t *base, uint32_t offset, uint8_t value)
{
    mmio_write8(base + offset, value);
}

static void write16(volatile uint8_t *base, uint32_t offset, uint16_t value)
{
    mmio_write16(base + offset, value);
}

static void write32(volatile uint8_t *base, uint32_t offset, uint32_t value)
{
    mmio_write32(base + offset, value);
}

static void write64(volatile uint8_t *base, uint32_t offset, uint64_t value)
{
    mmio_write32(base + offset, (uint32_t)value);
    mmio_write32(base + offset + 4, (uint32_t)(value >> 32));
}

/**
 * map the part of a BAR that a capability points at.
 */
static volatile uint8_t *map_cap(pci_device_t *pci, uint8_t cap)
{
    uint8_t bar = pci_read8(pci, (uint8_t)(cap + 4));
    uint32_t offset = pci_read32(pci, (uint8_t)(cap + 8));
    uint32_t length = pci_read32(pci, (uint8_t)(cap + 12));
    if (bar >= PCI_BAR_COUNT || pci->bars[bar].base == 0 || pci->bars[bar].is_io ||
        (uint64_t)offset + length > pci->bars[bar].size)
    {
        return NULL;
    }
    uint64_t phys = pci->bars[bar].base + offset;
    if ((uint64_t)(phys_addr_t)phys != phys)
    {
        return NULL; // above what this kernel can address (i386)
    }
    return ioremap_nocache((phys_addr_t)phys, length);
}

static bool find_registers(virtio_device_t *dev)
{
    pci_device_t *pci = dev->pci;
    if (!(pci_read16(pci, 0x06) & 0x10)) // no capability list
    {
        return false;
    }
    uint8_t cap = pci_read8(pci, 0x34) & 0xFCu;
    for (int guard = 0; cap != 0 && guard < 48; guard++)
    {
        if (pci_read8(pci, cap) == CAP_VENDOR)
        {
            uint8_t type = pci_read8(pci, (uint8_t)(cap + 3));
            if (type == CFG_COMMON && !dev->common)
            {
                dev->common = map_cap(pci, cap);
            }
            else if (type == CFG_NOTIFY && !dev->notify)
            {
                dev->notify = map_cap(pci, cap);
                dev->notify_multiplier = pci_read32(pci, (uint8_t)(cap + 16));
            }
            else if (type == CFG_ISR && !dev->isr)
            {
                dev->isr = map_cap(pci, cap);
            }
            else if (type == CFG_DEVICE && !dev->device)
            {
                dev->device = map_cap(pci, cap);
            }
        }
        cap = pci_read8(pci, (uint8_t)(cap + 1)) & 0xFCu;
    }
    return dev->common && dev->notify && dev->isr && dev->device;
}

bool virtio_init(virtio_device_t *dev, pci_device_t *pci, uint64_t features)
{
    memset(dev, 0, sizeof(*dev));
    dev->pci = pci;
    // memory decoding for the registers, bus mastering for the queues
    pci_enable_bus_master(pci);
    if (!find_registers(dev))
    {
        return false;
    }

    volatile uint8_t *c = dev->common;
    write8(c, DEVICE_STATUS, 0); // reset
    for (int i = 0; i < 1000000 && read8(c, DEVICE_STATUS) != 0; i++)
    {
    }
    write8(c, DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    write8(c, DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    // the features both sides have
    features |= 1ull << VIRTIO_F_VERSION_1;
    for (uint32_t word = 0; word < 2; word++)
    {
        write32(c, DEVICE_FEATURE_SELECT, word);
        uint32_t offered = mmio_read32(c + DEVICE_FEATURE);
        write32(c, DRIVER_FEATURE_SELECT, word);
        write32(c, DRIVER_FEATURE, offered & (uint32_t)(features >> (32 * word)));
    }
    write8(c, DEVICE_STATUS,
           VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    if (!(read8(c, DEVICE_STATUS) & VIRTIO_STATUS_FEATURES_OK))
    {
        write8(c, DEVICE_STATUS, VIRTIO_STATUS_FAILED);
        return false;
    }
    return true;
}

bool virtio_queue_init(virtio_device_t *dev, virtq_t *q, uint16_t index)
{
    memset(q, 0, sizeof(*q));
    volatile uint8_t *c = dev->common;
    write16(c, QUEUE_SELECT, index);
    uint16_t most = read16(c, QUEUE_SIZE);
    if (most == 0)
    {
        return false;
    }
    q->dev = dev;
    q->index = index;
    q->size = most < VIRTQ_SIZE ? most : VIRTQ_SIZE;
    q->ring = pmm_alloc_page();
    q->buffer = pmm_alloc_page();
    if (!q->ring || !q->buffer)
    {
        return false;
    }
    memset(q->ring, 0, PAGE_SIZE);
    spinlock_init(&q->lock);

    write16(c, QUEUE_SIZE, q->size);
    write16(c, QUEUE_MSIX_VECTOR, NO_VECTOR);
    phys_addr_t ring = virt_to_phys(q->ring);
    write64(c, QUEUE_DESC, ring);
    write64(c, QUEUE_DRIVER, ring + AVAIL_OFFSET);
    write64(c, QUEUE_DEVICE, ring + USED_OFFSET);
    uint16_t off = read16(c, QUEUE_NOTIFY_OFF);
    q->note = (volatile uint16_t *)(dev->notify + (uint32_t)off * dev->notify_multiplier);
    write16(c, QUEUE_ENABLE, 1);
    return true;
}

void virtio_use_config_vector(virtio_device_t *dev)
{
    write16(dev->common, MSIX_CONFIG, dev->pci->msix_offset ? 0 : NO_VECTOR);
}

void virtio_ready(virtio_device_t *dev)
{
    write8(dev->common, DEVICE_STATUS,
           VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK |
               VIRTIO_STATUS_DRIVER_OK);
}

bool virtq_call(virtq_t *q, const void *request, uint32_t request_size, void *response,
                uint32_t response_size)
{
    const uint32_t half = PAGE_SIZE / 2;
    if (!q->ring || request_size > half || response_size > half)
    {
        return false;
    }
    uint32_t flags = spin_lock_irqsave(&q->lock);

    // one request at a time: descriptors 0 and 1, buffers in the page
    memcpy(q->buffer, request, request_size);
    phys_addr_t buffer = virt_to_phys(q->buffer);
    desc_t *desc = (desc_t *)q->ring;
    desc[0] = (desc_t){buffer, request_size, DESC_NEXT, 1};
    desc[1] = (desc_t){buffer + half, response_size, DESC_WRITE, 0};

    volatile uint16_t *avail = (volatile uint16_t *)(q->ring + AVAIL_OFFSET);
    avail[2 + q->avail_next % q->size] = 0; // the chain starts at descriptor 0
    __sync_synchronize();
    q->avail_next++;
    avail[1] = q->avail_next;
    __sync_synchronize();
    *q->note = q->index;

    volatile uint16_t *used = (volatile uint16_t *)(q->ring + USED_OFFSET);
    bool done = false;
    for (uint32_t i = 0; i < WAIT_LOOPS; i++)
    {
        if (used[1] != q->used_seen)
        {
            done = true;
            break;
        }
        __builtin_ia32_pause();
    }
    if (done)
    {
        q->used_seen++;
        __sync_synchronize();
        memcpy(response, q->buffer + half, response_size);
    }
    spin_unlock_irqrestore(&q->lock, flags);
    return done;
}

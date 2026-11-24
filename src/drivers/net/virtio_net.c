/**
 * @file virtio_net.c
 * @brief the virtio network device (see virtio_net.h).
 *
 * queue 0 receives and queue 1 sends. each frame has a 12-byte header in
 * front of it (struct virtio_net_hdr with num_buffers). the driver asks
 * for no offloads, so the header is all zero on send and is skipped on
 * receive.
 *
 * buffers are 2 KiB, two to a page, one for each descriptor of a queue.
 * the receive buffers are all given to the device at the start, and each
 * is given back after its frame is handled. a send buffer is free again
 * when the device gives it back.
 */

#include "virtio_net.h"

#include "common/memory.h"
#include "drivers/pci/pci_irq.h"
#include "drivers/virtio/virtio.h"
#include "kernel/klog/klog.h"
#include "kernel/memory/mmio.h"
#include "kernel/memory/page.h"
#include "kernel/memory/phys.h"
#include "kernel/memory/pmm.h"
#include "kernel/net/netif.h"

#define DEVICE_ID_LEGACY 0x1000 // a transitional network device (QEMU's default)
#define DEVICE_ID_MODERN 0x1041

#define F_MAC 5 // the configuration has the MAC address

#define HEADER_SIZE 12
#define BUFFER_SIZE 2048

#define RECEIVE_QUEUE 0
#define SEND_QUEUE    1

typedef struct
{
    uint8_t *pages[VIRTQ_SIZE / 2];
    uint16_t count; // buffers, the size of the queue
} buffers_t;

static virtio_device_t device;
static virtq_t receive_queue;
static virtq_t send_queue;
static buffers_t receive_buffers;
static buffers_t send_buffers;
static uint16_t send_free[VIRTQ_SIZE]; // the free send buffers, a stack
static uint16_t send_free_count;
static netif_t *netif;

static uint8_t *buffer_at(buffers_t *b, uint16_t id)
{
    return b->pages[id / 2] + (id % 2) * BUFFER_SIZE;
}

static bool buffers_alloc(buffers_t *b, uint16_t count)
{
    b->count = count;
    for (uint16_t i = 0; i < (count + 1) / 2; i++)
    {
        b->pages[i] = pmm_alloc_page();
        if (!b->pages[i])
        {
            return false;
        }
        memset(b->pages[i], 0, PAGE_SIZE);
    }
    return true;
}

static void give_receive(uint16_t id)
{
    virtq_give(&receive_queue, id, virt_to_phys(buffer_at(&receive_buffers, id)), BUFFER_SIZE,
               true);
}

/**
 * put the send buffers the device is done with back on the free stack
 * (net_lock held).
 */
static void reclaim_send(void)
{
    uint16_t id;
    uint32_t length;
    while (virtq_take(&send_queue, &id, &length))
    {
        if (id < send_buffers.count && send_free_count < VIRTQ_SIZE)
        {
            send_free[send_free_count++] = id;
        }
    }
}

static bool net_send(netif_t *n, const void *frame, uint32_t length)
{
    (void)n;
    if (length > BUFFER_SIZE - HEADER_SIZE)
    {
        return false;
    }
    if (send_free_count == 0)
    {
        reclaim_send();
        if (send_free_count == 0)
        {
            return false;
        }
    }
    uint16_t id = send_free[--send_free_count];
    uint8_t *buffer = buffer_at(&send_buffers, id);
    memset(buffer, 0, HEADER_SIZE);
    memcpy(buffer + HEADER_SIZE, frame, length);
    return virtq_give(&send_queue, id, virt_to_phys(buffer), HEADER_SIZE + length, false);
}

/**
 * handle the received frames and give their buffers back (net_lock held).
 */
static void receive_all(void)
{
    uint16_t id;
    uint32_t length;
    while (virtq_take(&receive_queue, &id, &length))
    {
        if (id >= receive_buffers.count)
        {
            continue;
        }
        if (length > HEADER_SIZE)
        {
            netif_receive(netif, buffer_at(&receive_buffers, id) + HEADER_SIZE,
                          length - HEADER_SIZE);
        }
        else
        {
            netif->rx_dropped++;
        }
        give_receive(id);
    }
}

static void net_interrupt(void *context)
{
    (void)context;
    // reading the status clears an INTx; bit 0: a queue has used buffers
    uint8_t status = mmio_read8(device.isr);
    if (!(status & 1) && !device.pci->msix_offset)
    {
        return; // a shared line, not this device
    }
    uint32_t flags = net_lock();
    receive_all();
    reclaim_send();
    net_unlock(flags);
}

bool virtio_net_init(void)
{
    pci_device_t *pci = NULL;
    for (uint32_t i = 0; i < pci_device_count() && !pci; i++)
    {
        pci_device_t *d = pci_device_at(i);
        if (d->vendor_id == VIRTIO_VENDOR_ID &&
            (d->device_id == DEVICE_ID_MODERN || d->device_id == DEVICE_ID_LEGACY))
        {
            pci = d;
        }
    }
    if (!pci)
    {
        return false;
    }
    if (!virtio_init(&device, pci, 1ull << F_MAC) ||
        !virtio_queue_init(&device, &receive_queue, RECEIVE_QUEUE) ||
        !virtio_queue_init(&device, &send_queue, SEND_QUEUE) ||
        !buffers_alloc(&receive_buffers, receive_queue.size) ||
        !buffers_alloc(&send_buffers, send_queue.size))
    {
        KLOG_WARN("virtio-net: the device cannot be set up");
        return false;
    }

    uint8_t mac[ETH_ADDR_LEN];
    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++)
    {
        mac[i] = mmio_read8(device.device + i);
    }

    pci_irq_mode_t mode = pci_irq_attach(pci, net_interrupt, NULL, "virtio-net");
    if (mode == PCI_IRQ_MSIX)
    {
        virtio_queue_interrupt(&receive_queue);
        virtio_queue_interrupt(&send_queue);
    }
    else if (mode == PCI_IRQ_NONE)
    {
        KLOG_WARN("virtio-net: no interrupt");
        return false;
    }

    netif = netif_register("eth", mac, ETH_MTU, net_send, NULL);
    if (!netif)
    {
        return false;
    }
    for (uint16_t id = 0; id < send_buffers.count; id++)
    {
        send_free[send_free_count++] = id;
    }
    // the device may be told of buffers only after it is ready
    virtio_ready(&device);
    uint32_t flags = net_lock();
    for (uint16_t id = 0; id < receive_buffers.count; id++)
    {
        give_receive(id);
    }
    net_unlock(flags);

    KLOG_INFO("virtio-net: %s %02x:%02x:%02x:%02x:%02x:%02x (%s)", netif->name, mac[0], mac[1],
              mac[2], mac[3], mac[4], mac[5], pci_irq_mode_name(mode));
    return true;
}

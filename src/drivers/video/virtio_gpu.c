/**
 * @file virtio_gpu.c
 * @brief the virtio-gpu display (QEMU -vga virtio, or -device virtio-gpu).
 *
 * 2D only. a mode is a resource in guest RAM (contiguous frames) shown on
 * scanout 0; the host sees changes after a transfer and a flush, which
 * the display layer asks for (display_driver_t.flush). the size the
 * screen wants is the QEMU window: the device says when it changes with a
 * configuration interrupt.
 */

#include "virtio_gpu.h"

#include "common/memory.h"
#include "drivers/pci/pci_irq.h"
#include "drivers/video/display.h"
#include "drivers/video/fb/fb.h"
#include "drivers/virtio/virtio.h"
#include "kernel/klog/klog.h"
#include "kernel/memory/mmio.h"
#include "kernel/memory/page.h"
#include "kernel/memory/phys.h"
#include "kernel/memory/pmm.h"

#define DEVICE_ID 0x1050 // virtio 1.0 GPU (0x1040 + 16)

#define MAX_WIDTH  3840
#define MAX_HEIGHT 2400

// commands and responses
#define CMD_GET_DISPLAY_INFO        0x0100
#define CMD_RESOURCE_CREATE_2D      0x0101
#define CMD_RESOURCE_UNREF          0x0102
#define CMD_SET_SCANOUT             0x0103
#define CMD_RESOURCE_FLUSH          0x0104
#define CMD_TRANSFER_TO_HOST_2D     0x0105
#define CMD_RESOURCE_ATTACH_BACKING 0x0106
#define RESP_OK_NODATA              0x1100
#define RESP_OK_DISPLAY_INFO        0x1101

#define FORMAT_B8G8R8X8 2 // bytes B, G, R, X: x8r8g8b8 in a little-endian word
#define MAX_SCANOUTS    16
#define EVENT_DISPLAY   1

// the device configuration
#define CONFIG_EVENTS_READ  0
#define CONFIG_EVENTS_CLEAR 4

typedef struct
{
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t ring_idx;
    uint8_t padding[3];
} __attribute__((packed)) header_t;

typedef struct
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) rect_t;

typedef struct
{
    header_t hdr;
    struct
    {
        rect_t r;
        uint32_t enabled;
        uint32_t flags;
    } __attribute__((packed)) modes[MAX_SCANOUTS];
} __attribute__((packed)) display_info_t;

typedef struct
{
    header_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) create_2d_t;

typedef struct
{
    header_t hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) unref_t;

typedef struct
{
    header_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    uint64_t addr; // one entry: the backing is contiguous
    uint32_t length;
    uint32_t padding;
} __attribute__((packed)) attach_backing_t;

typedef struct
{
    header_t hdr;
    rect_t r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed)) set_scanout_t;

typedef struct
{
    header_t hdr;
    rect_t r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) transfer_t;

typedef struct
{
    header_t hdr;
    rect_t r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) flush_t;

static virtio_device_t device;
static virtq_t control;

// the mode on the screen
static uint32_t resource;  // 0: none yet
static uint32_t next_resource = 1;
static uint8_t *backing;   // direct-map pointer to its frames
static uint32_t pages;
static uint32_t width;
static uint32_t height;

/**
 * send a command that answers with no data.
 */
static bool command(void *request, uint32_t size)
{
    header_t response;
    return virtq_call(&control, request, size, &response, sizeof(response)) &&
           response.type == RESP_OK_NODATA;
}

static bool gpu_preferred(uint32_t *w, uint32_t *h)
{
    // clear the event that brought us here
    uint32_t events = mmio_read32(device.device + CONFIG_EVENTS_READ);
    if (events)
    {
        mmio_write32(device.device + CONFIG_EVENTS_CLEAR, events);
    }

    header_t request = {.type = CMD_GET_DISPLAY_INFO};
    static display_info_t info; // too large for the kernel stack
    if (!virtq_call(&control, &request, sizeof(request), &info, sizeof(info)))
    {
        KLOG_WARN("virtio-gpu: no answer to GET_DISPLAY_INFO");
        return false;
    }
    if (info.hdr.type != RESP_OK_DISPLAY_INFO || !info.modes[0].enabled)
    {
        KLOG_WARN("virtio-gpu: display info %x, enabled %u", info.hdr.type, info.modes[0].enabled);
        return false;
    }
    *w = info.modes[0].r.width;
    *h = info.modes[0].r.height;
    return *w > 0 && *h > 0;
}

static bool transfer(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    transfer_t t = {.hdr.type = CMD_TRANSFER_TO_HOST_2D,
                    .r = {x, y, w, h},
                    .offset = (uint64_t)y * width * 4 + x * 4,
                    .resource_id = resource};
    flush_t f = {.hdr.type = CMD_RESOURCE_FLUSH, .r = {x, y, w, h}, .resource_id = resource};
    return command(&t, sizeof(t)) && command(&f, sizeof(f));
}

static void gpu_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (resource)
    {
        transfer(x, y, w, h);
    }
}

static void free_frames(uint8_t *frames, uint32_t count)
{
    // a page that is still mapped by a program keeps its reference
    for (uint32_t i = 0; i < count; i++)
    {
        pmm_free_page(frames + i * PAGE_SIZE);
    }
}

static bool gpu_set_mode(uint32_t w, uint32_t h, display_surface_t *out)
{
    if (w == 0 || h == 0 || w > MAX_WIDTH || h > MAX_HEIGHT)
    {
        return false;
    }
    uint32_t count = (w * h * 4 + PAGE_SIZE - 1) / PAGE_SIZE;
    uint8_t *frames = pmm_alloc_contiguous(count);
    if (!frames)
    {
        KLOG_WARN("virtio-gpu: no %u contiguous pages for %ux%u", count, w, h);
        return false;
    }
    memset(frames, 0, count * PAGE_SIZE);

    uint32_t id = next_resource++;
    create_2d_t create = {.hdr.type = CMD_RESOURCE_CREATE_2D,
                          .resource_id = id,
                          .format = FORMAT_B8G8R8X8,
                          .width = w,
                          .height = h};
    attach_backing_t attach = {.hdr.type = CMD_RESOURCE_ATTACH_BACKING,
                               .resource_id = id,
                               .nr_entries = 1,
                               .addr = virt_to_phys(frames),
                               .length = w * h * 4};
    set_scanout_t scanout = {.hdr.type = CMD_SET_SCANOUT,
                             .r = {0, 0, w, h},
                             .scanout_id = 0,
                             .resource_id = id};
    if (!command(&create, sizeof(create)) || !command(&attach, sizeof(attach)) ||
        !command(&scanout, sizeof(scanout)))
    {
        KLOG_WARN("virtio-gpu: the device refused a %ux%u resource", w, h);
        unref_t unref = {.hdr.type = CMD_RESOURCE_UNREF, .resource_id = id};
        command(&unref, sizeof(unref));
        free_frames(frames, count);
        return false;
    }

    // the old mode goes (unref detaches its backing)
    if (resource)
    {
        unref_t unref = {.hdr.type = CMD_RESOURCE_UNREF, .resource_id = resource};
        command(&unref, sizeof(unref));
        free_frames(backing, pages);
    }
    resource = id;
    backing = frames;
    pages = count;
    width = w;
    height = h;
    transfer(0, 0, w, h);

    out->phys = virt_to_phys(frames);
    out->addr = frames;
    out->width = w;
    out->height = h;
    out->pitch = w * 4;
    return true;
}

static void gpu_interrupt(void *context)
{
    (void)context;
    // reading the status clears an INTx; with MSI-X the vector says it
    uint8_t status = mmio_read8(device.isr);
    if ((status & 2) || device.pci->msix_offset)
    {
        display_preferred_changed();
    }
}

static const display_driver_t gpu_driver = {
    .name = "virtio-gpu",
    .max_width = MAX_WIDTH,
    .max_height = MAX_HEIGHT,
    .set_mode = gpu_set_mode,
    .preferred = gpu_preferred,
    .flush = gpu_flush,
};

bool virtio_gpu_init(void)
{
    pci_device_t *pci = NULL;
    for (uint32_t i = 0; i < pci_device_count() && !pci; i++)
    {
        pci_device_t *d = pci_device_at(i);
        if (d->vendor_id == VIRTIO_VENDOR_ID && d->device_id == DEVICE_ID)
        {
            pci = d;
        }
    }
    if (!pci)
    {
        return false;
    }
    if (!virtio_init(&device, pci, 0) || !virtio_queue_init(&device, &control, 0))
    {
        KLOG_WARN("virtio-gpu: the device cannot be set up");
        return false;
    }
    if (pci_irq_attach(pci, gpu_interrupt, NULL, "virtio-gpu") != PCI_IRQ_NONE)
    {
        virtio_use_config_vector(&device);
    }
    virtio_ready(&device);
    display_attach(&gpu_driver);

    // start at the size the screen wants (the QEMU window)
    uint32_t w;
    uint32_t h;
    if (!gpu_preferred(&w, &h))
    {
        KLOG_WARN("virtio-gpu: the screen gives no size");
    }
    else if (fb_set_mode(w, h))
    {
        KLOG_INFO("virtio-gpu: %ux%u", w, h);
    }
    else
    {
        KLOG_WARN("virtio-gpu: cannot set %ux%u", w, h);
    }
    return true;
}

/**
 * a small xHCI driver for USB keyboards (eXtensible Host Controller
 * Interface 1.2; USB HID 1.11 boot protocol).
 *
 * the controller works with rings of 16-byte TRBs in RAM. the driver puts
 * commands on the command ring, transfers on each endpoint's transfer ring,
 * and reads the results from the event ring. a cycle bit in each TRB marks
 * which side owns it; it changes each time a ring wraps (through a link TRB
 * on the command and transfer rings).
 *
 * all waiting is by polling, during boot (interrupts off) and then from the
 * timer interrupt. each controller has one page for each ring, the device
 * context array, and one page per device for its contexts and its data.
 */

#include "xhci.h"

#include "arch/arch.h"
#include "common/memory.h"
#include "drivers/keyboard/keyboard.h"
#include "drivers/pci/pci.h"
#include "kernel/klog/klog.h"
#include "kernel/memory/mmio.h"
#include "kernel/memory/page.h"
#include "kernel/memory/pmm.h"
#include "kernel/time/timer.h"

#define MAX_CONTROLLERS 2
#define MAX_KEYBOARDS   4
#define RING_TRBS       256 // one page of TRBs; the last one is the link TRB
#define SPIN_LIMIT      50000000u

// capability registers
#define CAP_LENGTH  0x00
#define CAP_HCS1    0x04
#define CAP_HCS2    0x08
#define CAP_HCC1    0x10
#define CAP_DBOFF   0x14
#define CAP_RTSOFF  0x18

// operational registers
#define OP_USBCMD   0x00
#define OP_USBSTS   0x04
#define OP_CRCR     0x18
#define OP_DCBAAP   0x30
#define OP_CONFIG   0x38
#define OP_PORTS    0x400

#define USBCMD_RUN   0x1u
#define USBCMD_RESET 0x2u
#define USBSTS_HALTED 0x1u
#define USBSTS_NOT_READY (1u << 11)

// PORTSC bits
#define PORT_CONNECTED  (1u << 0)
#define PORT_ENABLED    (1u << 1)
#define PORT_RESET      (1u << 4)
#define PORT_POWER      (1u << 9)
#define PORT_RESET_DONE (1u << 21)
#define PORT_CHANGE_BITS 0x00FE0000u // the write-1-to-clear change bits
#define PORT_SPEED(v)   (((v) >> 10) & 0xFu)

#define SPEED_FULL  1
#define SPEED_LOW   2
#define SPEED_HIGH  3
#define SPEED_SUPER 4

// interrupter 0 (runtime registers)
#define IR0_ERSTSZ 0x28
#define IR0_ERSTBA 0x30
#define IR0_ERDP   0x38
#define ERDP_BUSY  0x8u

// TRB types
#define TRB_NORMAL        1
#define TRB_SETUP         2
#define TRB_DATA          3
#define TRB_STATUS        4
#define TRB_LINK          6
#define TRB_ENABLE_SLOT   9
#define TRB_ADDRESS_DEV   11
#define TRB_CONFIGURE_EP  12
#define TRB_EVALUATE_CTX  13
#define TRB_TRANSFER_EVT  32
#define TRB_COMMAND_EVT   33

#define TRB_CYCLE       (1u << 0)
#define TRB_TOGGLE      (1u << 1)  // link TRB: toggle the cycle bit
#define TRB_IOC         (1u << 5)  // interrupt (an event) on completion
#define TRB_IDT         (1u << 6)  // immediate data (setup TRB)
#define TRB_TYPE(t)     ((uint32_t)(t) << 10)
#define TRB_DIR_IN      (1u << 16)

#define COMPLETION_SUCCESS 1
#define COMPLETION_SHORT   13

// endpoint types in an endpoint context
#define EP_TYPE_CONTROL 4
#define EP_TYPE_INT_IN  7

// extended capability: USB legacy support (the BIOS handoff)
#define XCAP_LEGACY      1
#define LEGACY_BIOS_OWNS (1u << 16)
#define LEGACY_OS_OWNS   (1u << 24)

// key repeat (in timer ticks at 100 Hz)
#define REPEAT_DELAY 50
#define REPEAT_RATE  3

typedef struct
{
    uint32_t words[4]; // parameter low, parameter high, status, control
} trb_t;

typedef struct
{
    trb_t *trbs;
    phys_addr_t phys;
    uint32_t index;
    uint32_t cycle;
} ring_t;

typedef struct xhci xhci_t;

typedef struct
{
    xhci_t *hc;
    uint8_t slot;
    uint8_t dci;          // the device context index of the interrupt IN endpoint
    uint8_t *page;        // the contexts and the data of this device
    phys_addr_t page_phys;
    ring_t ep0;
    ring_t interrupt;
    uint8_t report[8];    // the last boot report
    uint8_t held_usage;   // the key that repeats, 0 if none
    uint64_t held_since;
} keyboard_t;

struct xhci
{
    volatile uint8_t *cap;
    volatile uint8_t *op;
    volatile uint8_t *runtime;
    volatile uint32_t *doorbells;
    uint32_t context_size; // 32 or 64 bytes
    uint32_t ports;
    uint64_t *dcbaa;
    ring_t commands;
    trb_t *events;
    phys_addr_t events_phys;
    uint32_t event_index;
    uint32_t event_cycle;
};

static xhci_t controllers[MAX_CONTROLLERS];
static uint32_t controller_count;
static keyboard_t keyboards[MAX_KEYBOARDS];
static uint32_t keyboard_count;

/* ---- registers and memory ---------------------------------------------- */

static uint32_t rd32(volatile uint8_t *base, uint32_t off)
{
    return mmio_read32(base + off);
}

static void wr32(volatile uint8_t *base, uint32_t off, uint32_t value)
{
    mmio_write32(base + off, value);
}

static void wr64(volatile uint8_t *base, uint32_t off, uint64_t value)
{
    wr32(base, off, (uint32_t)value);
    wr32(base, off + 4, (uint32_t)(value >> 32));
}

/**
 * a zeroed page for the controller: below 4 GiB, because the driver does
 * not check the controller's 64-bit capability.
 */
static void *dma_page(phys_addr_t *phys)
{
    void *page = pmm_alloc_page();
    if (!page)
    {
        return NULL;
    }
    *phys = virt_to_phys(page);
    uint64_t address = *phys;
    if ((address >> 32) != 0)
    {
        pmm_free_page(page);
        return NULL;
    }
    memset(page, 0, PAGE_SIZE);
    return page;
}

static bool ring_init(ring_t *ring)
{
    ring->trbs = dma_page(&ring->phys);
    if (!ring->trbs)
    {
        return false;
    }
    ring->index = 0;
    ring->cycle = 1;

    // the last TRB links back to the start and toggles the cycle bit
    trb_t *link = &ring->trbs[RING_TRBS - 1];
    link->words[0] = (uint32_t)ring->phys;
    link->words[1] = (uint32_t)((uint64_t)ring->phys >> 32);
    link->words[3] = TRB_TYPE(TRB_LINK) | TRB_TOGGLE;
    return true;
}

/**
 * put a TRB on a command or transfer ring.
 * @return the physical address of the TRB.
 */
static phys_addr_t ring_push(ring_t *ring, uint64_t param, uint32_t status, uint32_t control)
{
    trb_t *trb = &ring->trbs[ring->index];
    phys_addr_t phys = ring->phys + ring->index * sizeof(trb_t);
    trb->words[0] = (uint32_t)param;
    trb->words[1] = (uint32_t)(param >> 32);
    trb->words[2] = status;
    trb->words[3] = (control & ~TRB_CYCLE) | ring->cycle;

    ring->index++;
    if (ring->index == RING_TRBS - 1)
    {
        trb_t *link = &ring->trbs[RING_TRBS - 1];
        link->words[3] = (link->words[3] & ~TRB_CYCLE) | ring->cycle;
        ring->index = 0;
        ring->cycle ^= 1u;
    }
    return phys;
}

/* ---- events -------------------------------------------------------------- */

/**
 * the next event, or NULL if there is none. the event stays valid until
 * the next call.
 */
static trb_t *next_event(xhci_t *hc)
{
    trb_t *event = &hc->events[hc->event_index];
    if ((event->words[3] & TRB_CYCLE) != hc->event_cycle)
    {
        return NULL;
    }

    hc->event_index++;
    if (hc->event_index == RING_TRBS)
    {
        hc->event_index = 0;
        hc->event_cycle ^= 1u;
    }
    phys_addr_t dequeue = hc->events_phys + hc->event_index * sizeof(trb_t);
    wr64(hc->runtime, IR0_ERDP, dequeue | ERDP_BUSY);
    return event;
}

static uint32_t event_type(const trb_t *event)
{
    return (event->words[3] >> 10) & 0x3Fu;
}

static uint32_t event_code(const trb_t *event)
{
    return event->words[2] >> 24;
}

static void handle_transfer_event(xhci_t *hc, const trb_t *event);

/**
 * wait for the event of a command or a transfer: a command completion for
 * the TRB at trb_phys, or a transfer event for it. other transfer events
 * (keyboard reports) are handled on the way.
 * @return the completion code (0 on a timeout); *slot gets the slot id.
 */
static uint32_t wait_for(xhci_t *hc, uint32_t type, phys_addr_t trb_phys, uint8_t *slot)
{
    for (uint32_t spin = 0; spin < SPIN_LIMIT; spin++)
    {
        trb_t *event = next_event(hc);
        if (!event)
        {
            continue;
        }

        uint64_t param = (uint64_t)event->words[0] | ((uint64_t)event->words[1] << 32);
        if (event_type(event) == type && param == trb_phys)
        {
            if (slot)
            {
                *slot = (uint8_t)(event->words[3] >> 24);
            }
            return event_code(event);
        }
        if (event_type(event) == TRB_TRANSFER_EVT)
        {
            handle_transfer_event(hc, event);
        }
    }
    return 0;
}

static uint32_t run_command(xhci_t *hc, uint64_t param, uint32_t control, uint8_t *slot)
{
    phys_addr_t trb = ring_push(&hc->commands, param, 0, control);
    mmio_write32(&hc->doorbells[0], 0);
    return wait_for(hc, TRB_COMMAND_EVT, trb, slot);
}

/* ---- contexts ------------------------------------------------------------ */

// the device page: the input context at 0, the output context at 0x800,
// the data buffer at 0xC00 (up to 1 KiB)
#define PAGE_INPUT   0x000
#define PAGE_OUTPUT  0x800
#define PAGE_DATA    0xC00
#define DATA_MAX     0x400

static uint32_t *input_ctx(keyboard_t *kb, uint32_t index)
{
    // index 0 is the input control context, 1 the slot, 2 endpoint 0, ...
    return (uint32_t *)(kb->page + PAGE_INPUT + index * kb->hc->context_size);
}

static void clear_input(keyboard_t *kb)
{
    memset(kb->page + PAGE_INPUT, 0, PAGE_OUTPUT - PAGE_INPUT);
}

static uint32_t ep0_packet_size(uint32_t speed)
{
    switch (speed)
    {
    case SPEED_LOW:
    case SPEED_FULL:
        return 8;
    case SPEED_HIGH:
        return 64;
    default:
        return 512;
    }
}

/* ---- control transfers --------------------------------------------------- */

/**
 * a control transfer on endpoint 0. data goes to or comes from the data
 * area of the device page.
 * @return true on success (a short read is a success).
 */
static bool control(keyboard_t *kb, uint8_t request_type, uint8_t request, uint16_t value,
                    uint16_t index, uint16_t length)
{
    bool in = (request_type & 0x80) != 0;
    uint64_t setup = (uint64_t)request_type | ((uint64_t)request << 8) |
                     ((uint64_t)value << 16) | ((uint64_t)index << 32) |
                     ((uint64_t)length << 48);
    uint32_t transfer_type = length == 0 ? 0 : (in ? 3u : 2u);

    ring_push(&kb->ep0, setup, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | (transfer_type << 16));
    if (length > 0)
    {
        ring_push(&kb->ep0, kb->page_phys + PAGE_DATA, length,
                  TRB_TYPE(TRB_DATA) | (in ? TRB_DIR_IN : 0));
    }
    // the status stage goes the other way (IN when there is no data)
    phys_addr_t status = ring_push(&kb->ep0, 0, 0,
                                   TRB_TYPE(TRB_STATUS) | TRB_IOC |
                                       ((in && length > 0) ? 0 : TRB_DIR_IN));
    mmio_write32(&kb->hc->doorbells[kb->slot], 1); // endpoint 0

    uint32_t code = wait_for(kb->hc, TRB_TRANSFER_EVT, status, NULL);
    return code == COMPLETION_SUCCESS || code == COMPLETION_SHORT;
}

/* ---- keyboard reports ---------------------------------------------------- */

// HID usage (0x04 to 0x64) to PS/2 set 1 scan code; 0x100 marks an 0xE0
// code, 0 a key the kernel does not use
static const uint16_t usage_to_scancode[0x65] = {
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20, [0x08] = 0x12, [0x09] = 0x21,
    [0x0A] = 0x22, [0x0B] = 0x23, [0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26,
    [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19, [0x14] = 0x10, [0x15] = 0x13,
    [0x16] = 0x1F, [0x17] = 0x14, [0x18] = 0x16, [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
    [0x1C] = 0x15, [0x1D] = 0x2C, [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05,
    [0x22] = 0x06, [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09, [0x26] = 0x0A, [0x27] = 0x0B,
    [0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F, [0x2C] = 0x39, [0x2D] = 0x0C,
    [0x2E] = 0x0D, [0x2F] = 0x1A, [0x30] = 0x1B, [0x31] = 0x2B, [0x32] = 0x2B, [0x33] = 0x27,
    [0x34] = 0x28, [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34, [0x38] = 0x35, [0x39] = 0x3A,
    [0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D, [0x3D] = 0x3E, [0x3E] = 0x3F, [0x3F] = 0x40,
    [0x40] = 0x41, [0x41] = 0x42, [0x42] = 0x43, [0x43] = 0x44, [0x44] = 0x57, [0x45] = 0x58,
    [0x47] = 0x46, [0x49] = 0x152, [0x4A] = 0x147, [0x4B] = 0x149, [0x4C] = 0x153,
    [0x4D] = 0x14F, [0x4E] = 0x151, [0x4F] = 0x14D, [0x50] = 0x14B, [0x51] = 0x150,
    [0x52] = 0x148, [0x53] = 0x45, [0x54] = 0x135, [0x55] = 0x37, [0x56] = 0x4A, [0x57] = 0x4E,
    [0x58] = 0x11C, [0x59] = 0x4F, [0x5A] = 0x50, [0x5B] = 0x51, [0x5C] = 0x4B, [0x5D] = 0x4C,
    [0x5E] = 0x4D, [0x5F] = 0x47, [0x60] = 0x48, [0x61] = 0x49, [0x62] = 0x52, [0x63] = 0x53,
    [0x64] = 0x56,
};

// the modifier bits of a boot report: left ctrl, shift, alt, GUI, then right
static const uint16_t modifier_scancode[8] = {0x1D, 0x2A, 0x38, 0x15B,
                                              0x11D, 0x36, 0x138, 0x15C};

static void send_scancode(uint16_t code, bool released)
{
    if (code == 0)
    {
        return;
    }
    if (code & 0x100)
    {
        keyboard_handle_scancode(0xE0);
    }
    keyboard_handle_scancode((uint8_t)((code & 0x7F) | (released ? 0x80 : 0)));
}

static uint16_t scancode_of(uint8_t usage)
{
    return usage < sizeof(usage_to_scancode) / sizeof(usage_to_scancode[0])
               ? usage_to_scancode[usage]
               : 0;
}

static bool report_has(const uint8_t *report, uint8_t usage)
{
    for (int i = 2; i < 8; i++)
    {
        if (report[i] == usage)
        {
            return true;
        }
    }
    return false;
}

/**
 * compare a new boot report with the last one: send a release for each key
 * that is gone and a press for each new key.
 */
static void handle_report(keyboard_t *kb, const uint8_t *report)
{
    if (report[2] == 1) // ErrorRollOver: too many keys; keep the last state
    {
        return;
    }

    uint8_t changed = report[0] ^ kb->report[0];
    for (int bit = 0; bit < 8; bit++)
    {
        if (changed & (1u << bit))
        {
            send_scancode(modifier_scancode[bit], !(report[0] & (1u << bit)));
        }
    }

    for (int i = 2; i < 8; i++)
    {
        uint8_t usage = kb->report[i];
        if (usage > 3 && !report_has(report, usage))
        {
            send_scancode(scancode_of(usage), true);
            if (usage == kb->held_usage)
            {
                kb->held_usage = 0;
            }
        }
    }
    for (int i = 2; i < 8; i++)
    {
        uint8_t usage = report[i];
        if (usage > 3 && !report_has(kb->report, usage))
        {
            send_scancode(scancode_of(usage), false);
            kb->held_usage = usage; // the newest key repeats
            kb->held_since = timer_now();
        }
    }

    memcpy(kb->report, report, sizeof(kb->report));
}

/**
 * queue a transfer for the next report on the interrupt endpoint.
 */
static void queue_report(keyboard_t *kb)
{
    ring_push(&kb->interrupt, kb->page_phys + PAGE_DATA, 8, TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    mmio_write32(&kb->hc->doorbells[kb->slot], kb->dci);
}

static void handle_transfer_event(xhci_t *hc, const trb_t *event)
{
    uint8_t slot = (uint8_t)(event->words[3] >> 24);
    uint8_t endpoint = (uint8_t)((event->words[3] >> 16) & 0x1F);
    uint32_t code = event_code(event);

    for (uint32_t i = 0; i < keyboard_count; i++)
    {
        keyboard_t *kb = &keyboards[i];
        if (kb->hc != hc || kb->slot != slot || kb->dci != endpoint)
        {
            continue;
        }
        if (code == COMPLETION_SUCCESS || code == COMPLETION_SHORT)
        {
            handle_report(kb, kb->page + PAGE_DATA);
        }
        queue_report(kb);
        return;
    }
}

/**
 * the timer hook: read the events of all controllers and repeat a held
 * key.
 */
static void xhci_poll(uint64_t now)
{
    for (uint32_t c = 0; c < controller_count; c++)
    {
        trb_t *event;
        while ((event = next_event(&controllers[c])) != NULL)
        {
            if (event_type(event) == TRB_TRANSFER_EVT)
            {
                handle_transfer_event(&controllers[c], event);
            }
        }
    }

    for (uint32_t i = 0; i < keyboard_count; i++)
    {
        keyboard_t *kb = &keyboards[i];
        if (kb->held_usage && now - kb->held_since >= REPEAT_DELAY &&
            (now - kb->held_since - REPEAT_DELAY) % REPEAT_RATE == 0)
        {
            send_scancode(scancode_of(kb->held_usage), false);
        }
    }
}

/* ---- device setup -------------------------------------------------------- */

/**
 * find the interrupt IN endpoint of a boot keyboard interface in a
 * configuration descriptor.
 * @return true if found (*endpoint, *packet, *interval and *interface set).
 */
static bool find_keyboard(const uint8_t *config, uint32_t length, uint8_t *interface,
                          uint8_t *endpoint, uint16_t *packet, uint8_t *interval)
{
    bool in_keyboard = false;
    for (uint32_t off = 0; off + 2 <= length;)
    {
        uint8_t size = config[off];
        uint8_t type = config[off + 1];
        if (size < 2 || off + size > length)
        {
            break;
        }

        if (type == 4 && size >= 9) // interface: class 3 (HID), subclass 1, protocol 1
        {
            in_keyboard = config[off + 5] == 3 && config[off + 6] == 1 && config[off + 7] == 1;
            if (in_keyboard)
            {
                *interface = config[off + 2];
            }
        }
        else if (type == 5 && size >= 7 && in_keyboard)
        {
            uint8_t address = config[off + 2];
            uint8_t attributes = config[off + 3];
            if ((address & 0x80) && (attributes & 3) == 3) // IN, interrupt
            {
                *endpoint = address & 0x0F;
                *packet = (uint16_t)(config[off + 4] | (config[off + 5] << 8));
                *interval = config[off + 6];
                return true;
            }
        }
        off += size;
    }
    return false;
}

/**
 * the xHCI interval (2^n * 125 us) for an endpoint's bInterval.
 */
static uint32_t xhci_interval(uint32_t speed, uint8_t interval)
{
    if (speed == SPEED_HIGH || speed == SPEED_SUPER)
    {
        return interval > 0 ? (uint32_t)(interval - 1) : 0;
    }
    // full and low speed: bInterval is in frames of 1 ms = 8 x 125 us
    uint32_t frames = interval ? interval : 1;
    uint32_t n = 3;
    while ((1u << (n + 1)) <= frames * 8 && n < 10)
    {
        n++;
    }
    return n;
}

static bool setup_device(xhci_t *hc, uint32_t port, uint32_t speed)
{
    if (keyboard_count >= MAX_KEYBOARDS)
    {
        return false;
    }
    keyboard_t *kb = &keyboards[keyboard_count];
    memset(kb, 0, sizeof(*kb));
    kb->hc = hc;

    if (run_command(hc, 0, TRB_TYPE(TRB_ENABLE_SLOT), &kb->slot) != COMPLETION_SUCCESS ||
        kb->slot == 0)
    {
        return false;
    }

    kb->page = dma_page(&kb->page_phys);
    if (!kb->page || !ring_init(&kb->ep0))
    {
        return false;
    }
    hc->dcbaa[kb->slot] = kb->page_phys + PAGE_OUTPUT;

    // address the device: the slot and endpoint 0
    clear_input(kb);
    input_ctx(kb, 0)[1] = 0x3; // add the slot and endpoint 0
    uint32_t *slot_ctx = input_ctx(kb, 1);
    slot_ctx[0] = (speed << 20) | (1u << 27); // one context entry
    slot_ctx[1] = (port + 1) << 16;           // the root hub port (1-based)
    uint32_t *ep0 = input_ctx(kb, 2);
    ep0[1] = (3u << 1) | (EP_TYPE_CONTROL << 3) | (ep0_packet_size(speed) << 16);
    ep0[2] = (uint32_t)kb->ep0.phys | 1u; // dequeue pointer, cycle state 1
    ep0[3] = (uint32_t)((uint64_t)kb->ep0.phys >> 32);
    ep0[4] = 8; // average TRB length

    if (run_command(hc, kb->page_phys + PAGE_INPUT,
                    TRB_TYPE(TRB_ADDRESS_DEV) | ((uint32_t)kb->slot << 24),
                    NULL) != COMPLETION_SUCCESS)
    {
        return false;
    }

    // the first 8 bytes of the device descriptor give the real endpoint 0
    // packet size (full speed devices can have 8 to 64)
    uint8_t *data = kb->page + PAGE_DATA;
    if (!control(kb, 0x80, 6, 0x0100, 0, 8))
    {
        return false;
    }
    uint32_t packet = data[7];
    if (speed == SPEED_FULL && packet != ep0_packet_size(speed) && packet >= 8)
    {
        clear_input(kb);
        input_ctx(kb, 0)[1] = 0x2; // evaluate endpoint 0
        input_ctx(kb, 2)[1] = (3u << 1) | (EP_TYPE_CONTROL << 3) | (packet << 16);
        run_command(hc, kb->page_phys + PAGE_INPUT,
                    TRB_TYPE(TRB_EVALUATE_CTX) | ((uint32_t)kb->slot << 24), NULL);
    }

    // the configuration descriptor: first its length, then all of it
    if (!control(kb, 0x80, 6, 0x0200, 0, 9))
    {
        return false;
    }
    uint16_t total = (uint16_t)(data[2] | (data[3] << 8));
    uint8_t config_value = data[5];
    if (total > DATA_MAX)
    {
        total = DATA_MAX;
    }
    if (!control(kb, 0x80, 6, 0x0200, 0, total))
    {
        return false;
    }

    uint8_t interface = 0;
    uint8_t endpoint = 0;
    uint16_t max_packet = 8;
    uint8_t interval = 10;
    if (!find_keyboard(data, total, &interface, &endpoint, &max_packet, &interval))
    {
        return false; // not a keyboard: leave the slot as it is
    }

    // SET_CONFIGURATION, then the boot protocol and no idle reports
    if (!control(kb, 0x00, 9, config_value, 0, 0) ||
        !control(kb, 0x21, 0x0B, 0, interface, 0) || // SET_PROTOCOL(boot)
        !control(kb, 0x21, 0x0A, 0, interface, 0))   // SET_IDLE(0)
    {
        return false;
    }

    // configure the interrupt IN endpoint
    if (!ring_init(&kb->interrupt))
    {
        return false;
    }
    kb->dci = (uint8_t)(endpoint * 2 + 1);
    clear_input(kb);
    input_ctx(kb, 0)[1] = 1u | (1u << kb->dci); // the slot and the endpoint
    slot_ctx = input_ctx(kb, 1);
    slot_ctx[0] = (speed << 20) | ((uint32_t)kb->dci << 27);
    slot_ctx[1] = (port + 1) << 16;
    uint32_t *ep = input_ctx(kb, (uint32_t)kb->dci + 1);
    ep[0] = xhci_interval(speed, interval) << 16;
    ep[1] = (3u << 1) | (EP_TYPE_INT_IN << 3) | ((uint32_t)max_packet << 16);
    ep[2] = (uint32_t)kb->interrupt.phys | 1u;
    ep[3] = (uint32_t)((uint64_t)kb->interrupt.phys >> 32);
    ep[4] = 8u | ((uint32_t)max_packet << 16); // average TRB length, max payload

    if (run_command(hc, kb->page_phys + PAGE_INPUT,
                    TRB_TYPE(TRB_CONFIGURE_EP) | ((uint32_t)kb->slot << 24),
                    NULL) != COMPLETION_SUCCESS)
    {
        return false;
    }

    keyboard_count++;
    queue_report(kb);
    KLOG_INFO("xhci: keyboard on port %u (slot %u)", port + 1, kb->slot);
    return true;
}

/**
 * reset a port with a device so it is enabled. USB 3 ports enable
 * themselves; USB 2 ports need a reset.
 * @return the port speed, or 0 if the port is not usable.
 */
static uint32_t reset_port(xhci_t *hc, uint32_t port)
{
    uint32_t reg = OP_PORTS + port * 0x10;
    uint32_t value = rd32(hc->op, reg);
    if (!(value & PORT_CONNECTED))
    {
        return 0;
    }

    if (!(value & PORT_ENABLED))
    {
        // keep the power bit, do not clear change bits by accident
        wr32(hc->op, reg, (value & ~(PORT_CHANGE_BITS | PORT_ENABLED)) | PORT_POWER | PORT_RESET);
        uint32_t spin = 0;
        while (!(rd32(hc->op, reg) & PORT_RESET_DONE) && spin++ < SPIN_LIMIT)
        {
        }
        value = rd32(hc->op, reg);
        wr32(hc->op, reg, (value & ~(PORT_CHANGE_BITS | PORT_ENABLED)) | PORT_RESET_DONE);
        value = rd32(hc->op, reg);
        if (!(value & PORT_ENABLED))
        {
            return 0;
        }
    }
    return PORT_SPEED(value);
}

/**
 * take the controller from the BIOS (the USB legacy support capability).
 */
static void bios_handoff(xhci_t *hc)
{
    uint32_t offset = (rd32(hc->cap, CAP_HCC1) >> 16) * 4;
    for (int guard = 0; offset != 0 && guard < 64; guard++)
    {
        uint32_t value = rd32(hc->cap, offset);
        if ((value & 0xFF) == XCAP_LEGACY)
        {
            wr32(hc->cap, offset, value | LEGACY_OS_OWNS);
            for (uint32_t spin = 0;
                 (rd32(hc->cap, offset) & LEGACY_BIOS_OWNS) && spin < SPIN_LIMIT; spin++)
            {
            }
            // no SMIs from the controller to the firmware any more
            wr32(hc->cap, offset + 4, rd32(hc->cap, offset + 4) & 0xFFFF1FEEu);
            return;
        }
        uint32_t next = (value >> 8) & 0xFF;
        offset = next ? offset + next * 4 : 0;
    }
}

static bool wait_status(xhci_t *hc, uint32_t mask, uint32_t want)
{
    for (uint32_t spin = 0; spin < SPIN_LIMIT; spin++)
    {
        if ((rd32(hc->op, OP_USBSTS) & mask) == want)
        {
            return true;
        }
    }
    return false;
}

static bool controller_init(xhci_t *hc, const pci_device_t *dev)
{
    if (dev->bars[0].is_io || dev->bars[0].base == 0 || (dev->bars[0].base >> 32) != 0)
    {
        return false;
    }

    size_t size = dev->bars[0].size ? (size_t)dev->bars[0].size : 0x10000;
    hc->cap = ioremap_nocache((phys_addr_t)dev->bars[0].base, size);
    if (!hc->cap)
    {
        return false;
    }
    pci_enable_bus_master(dev);
    bios_handoff(hc);

    hc->op = hc->cap + (rd32(hc->cap, CAP_LENGTH) & 0xFF);
    hc->runtime = hc->cap + (rd32(hc->cap, CAP_RTSOFF) & ~0x1Fu);
    hc->doorbells = (volatile uint32_t *)(hc->cap + (rd32(hc->cap, CAP_DBOFF) & ~0x3u));
    hc->context_size = (rd32(hc->cap, CAP_HCC1) & 0x4) ? 64 : 32;
    uint32_t hcs1 = rd32(hc->cap, CAP_HCS1);
    uint32_t slots = hcs1 & 0xFF;
    hc->ports = hcs1 >> 24;

    // stop and reset
    wr32(hc->op, OP_USBCMD, rd32(hc->op, OP_USBCMD) & ~USBCMD_RUN);
    if (!wait_status(hc, USBSTS_HALTED, USBSTS_HALTED))
    {
        return false;
    }
    wr32(hc->op, OP_USBCMD, USBCMD_RESET);
    for (uint32_t spin = 0; (rd32(hc->op, OP_USBCMD) & USBCMD_RESET) && spin < SPIN_LIMIT; spin++)
    {
    }
    if (!wait_status(hc, USBSTS_NOT_READY, 0))
    {
        return false;
    }

    if (slots > 8)
    {
        slots = 8;
    }
    wr32(hc->op, OP_CONFIG, slots);

    // the device context base address array, with the scratchpad buffers
    // the controller asks for in entry 0
    phys_addr_t dcbaa_phys;
    hc->dcbaa = dma_page(&dcbaa_phys);
    if (!hc->dcbaa)
    {
        return false;
    }
    uint32_t hcs2 = rd32(hc->cap, CAP_HCS2);
    uint32_t scratchpads = ((hcs2 >> 27) & 0x1F) | (((hcs2 >> 21) & 0x1F) << 5);
    if (scratchpads > 0)
    {
        phys_addr_t array_phys;
        uint64_t *array = dma_page(&array_phys);
        if (!array || scratchpads > PAGE_SIZE / 8)
        {
            return false;
        }
        for (uint32_t i = 0; i < scratchpads; i++)
        {
            phys_addr_t buffer;
            if (!dma_page(&buffer))
            {
                return false;
            }
            array[i] = buffer;
        }
        hc->dcbaa[0] = array_phys;
    }
    wr64(hc->op, OP_DCBAAP, dcbaa_phys);

    // the command ring
    if (!ring_init(&hc->commands))
    {
        return false;
    }
    wr64(hc->op, OP_CRCR, hc->commands.phys | 1u);

    // the event ring and its segment table (one segment)
    phys_addr_t erst_phys;
    uint64_t *erst = dma_page(&erst_phys);
    hc->events = dma_page(&hc->events_phys);
    if (!erst || !hc->events)
    {
        return false;
    }
    erst[0] = hc->events_phys;
    erst[1] = RING_TRBS;
    hc->event_index = 0;
    hc->event_cycle = 1;
    wr32(hc->runtime, IR0_ERSTSZ, 1);
    wr64(hc->runtime, IR0_ERDP, hc->events_phys);
    wr64(hc->runtime, IR0_ERSTBA, erst_phys);

    // run (no interrupts: the events are polled)
    wr32(hc->op, OP_USBCMD, USBCMD_RUN);
    return wait_status(hc, USBSTS_HALTED, 0);
}

uint32_t xhci_init(void)
{
    for (uint32_t n = 0; controller_count < MAX_CONTROLLERS; n++)
    {
        pci_device_t *dev = pci_find_class(PCI_CLASS_SERIAL, PCI_SUBCLASS_USB, 0x30, n);
        if (!dev)
        {
            break;
        }

        xhci_t *hc = &controllers[controller_count];
        memset(hc, 0, sizeof(*hc));
        if (!controller_init(hc, dev))
        {
            KLOG_WARN("xhci: controller %u could not be started", n);
            continue;
        }
        controller_count++;

        // give the ports time to see their devices after the reset
        for (volatile uint32_t spin = 0; spin < 2000000u; spin++)
        {
        }

        for (uint32_t port = 0; port < hc->ports; port++)
        {
            uint32_t speed = reset_port(hc, port);
            if (speed != 0)
            {
                setup_device(hc, port, speed);
            }
        }
    }

    if (controller_count > 0)
    {
        timer_add_poll_hook(xhci_poll);
    }
    KLOG_INFO("xhci: %u controller(s), %u keyboard(s)", controller_count, keyboard_count);
    return keyboard_count;
}

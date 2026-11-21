/**
 * PCI enumeration with configuration mechanism #1: write the address of a
 * register (bus, device, function, offset) to port 0xCF8, then read or
 * write its 32-bit value at port 0xCFC.
 *
 * locking: pci_init runs once at boot before other users; later accesses
 * are each one out/in pair with interrupts off.
 */

#include "pci.h"

#include "arch/arch.h"
#include "arch/x86/cpu/io.h"
#include "common/memory.h"
#include "kernel/klog/klog.h"

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA    0xCFC

#define PCI_VENDOR_ID    0x00
#define PCI_DEVICE_ID    0x02
#define PCI_STATUS       0x06
#define PCI_REVISION     0x08
#define PCI_PROG_IF      0x09
#define PCI_SUBCLASS     0x0A
#define PCI_CLASS        0x0B
#define PCI_HEADER_TYPE  0x0E
#define PCI_BAR0         0x10
#define PCI_CAPABILITIES 0x34
#define PCI_IRQ_LINE     0x3C
#define PCI_IRQ_PIN      0x3D

#define STATUS_CAPABILITIES 0x0010
#define CAP_MSI             0x05
#define CAP_MSIX            0x11

static pci_device_t devices[PCI_MAX_DEVICES];
static uint32_t device_count;

static uint32_t config_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(device & 0x1F) << 11) |
           ((uint32_t)(function & 0x7) << 8) | (offset & 0xFCu);
}

static uint32_t raw_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t flags = arch_irq_save();
    io_outl(CONFIG_ADDRESS, config_address(bus, device, function, offset));
    uint32_t value = io_inl(CONFIG_DATA);
    arch_irq_restore(flags);
    return value;
}

static void raw_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset,
                        uint32_t value)
{
    uint32_t flags = arch_irq_save();
    io_outl(CONFIG_ADDRESS, config_address(bus, device, function, offset));
    io_outl(CONFIG_DATA, value);
    arch_irq_restore(flags);
}

uint32_t pci_read32(const pci_device_t *dev, uint8_t offset)
{
    return raw_read32(dev->bus, dev->device, dev->function, offset);
}

uint16_t pci_read16(const pci_device_t *dev, uint8_t offset)
{
    return (uint16_t)(pci_read32(dev, offset) >> ((offset & 2u) * 8));
}

uint8_t pci_read8(const pci_device_t *dev, uint8_t offset)
{
    return (uint8_t)(pci_read32(dev, offset) >> ((offset & 3u) * 8));
}

void pci_write32(const pci_device_t *dev, uint8_t offset, uint32_t value)
{
    raw_write32(dev->bus, dev->device, dev->function, offset, value);
}

void pci_write16(const pci_device_t *dev, uint8_t offset, uint16_t value)
{
    uint8_t aligned = offset & 0xFCu;
    uint32_t shift = (offset & 2u) * 8;
    uint32_t word = pci_read32(dev, aligned);
    word = (word & ~(0xFFFFu << shift)) | ((uint32_t)value << shift);
    pci_write32(dev, aligned, word);
}

void pci_enable_bus_master(const pci_device_t *dev)
{
    uint16_t command = pci_read16(dev, PCI_COMMAND);
    pci_write16(dev, PCI_COMMAND, command | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER);
}

/**
 * find the size of each BAR: write all ones, read back the mask, write the
 * old value again. decoding is off while the BAR holds the mask, so the
 * device does not answer at a wrong address.
 */
static void read_bars(pci_device_t *dev)
{
    // only type 0 headers have 6 BARs; a bridge has 2
    uint32_t count = (dev->header_type & 0x7F) == 0 ? 6 : ((dev->header_type & 0x7F) == 1 ? 2 : 0);

    uint16_t command = pci_read16(dev, PCI_COMMAND);
    pci_write16(dev, PCI_COMMAND, command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));

    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t offset = (uint8_t)(PCI_BAR0 + i * 4);
        uint32_t original = pci_read32(dev, offset);
        pci_write32(dev, offset, 0xFFFFFFFFu);
        uint32_t mask = pci_read32(dev, offset);
        pci_write32(dev, offset, original);

        pci_bar_t *bar = &dev->bars[i];
        if (mask == 0 || mask == 0xFFFFFFFFu)
        {
            continue; // not used
        }

        if (original & 1u)
        {
            bar->is_io = true;
            bar->base = original & ~3u;
            bar->size = (~(mask & ~3u) + 1u) & 0xFFFFu;
            continue;
        }

        uint32_t type = (original >> 1) & 3u;
        bar->prefetchable = (original & 8u) != 0;
        uint64_t base = original & ~0xFu;
        uint64_t size_mask = 0xFFFFFFFF00000000ull | (mask & ~0xFu);

        if (type == 2 && i + 1 < count)
        {
            // a 64-bit BAR: the next one holds the high half
            uint8_t high_offset = (uint8_t)(offset + 4);
            uint32_t high = pci_read32(dev, high_offset);
            pci_write32(dev, high_offset, 0xFFFFFFFFu);
            uint32_t high_mask = pci_read32(dev, high_offset);
            pci_write32(dev, high_offset, high);

            bar->is_64 = true;
            base |= (uint64_t)high << 32;
            size_mask = ((uint64_t)high_mask << 32) | (mask & ~0xFu);
            i++; // the next BAR is the high half
        }

        bar->base = base;
        bar->size = ~size_mask + 1u;
    }

    pci_write16(dev, PCI_COMMAND, command);
}

/**
 * find the MSI and MSI-X capabilities in the capability list.
 */
static void read_capabilities(pci_device_t *dev)
{
    if (!(pci_read16(dev, PCI_STATUS) & STATUS_CAPABILITIES))
    {
        return;
    }

    uint8_t offset = pci_read8(dev, PCI_CAPABILITIES) & 0xFCu;
    for (int guard = 0; offset != 0 && guard < 48; guard++)
    {
        uint8_t id = pci_read8(dev, offset);
        if (id == CAP_MSI)
        {
            dev->msi_offset = offset;
        }
        else if (id == CAP_MSIX)
        {
            dev->msix_offset = offset;
        }
        offset = pci_read8(dev, (uint8_t)(offset + 1)) & 0xFCu;
    }
}

static void add_function(uint8_t bus, uint8_t device, uint8_t function)
{
    if (device_count >= PCI_MAX_DEVICES)
    {
        return;
    }

    pci_device_t *dev = &devices[device_count++];
    memset(dev, 0, sizeof(*dev));
    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->vendor_id = pci_read16(dev, PCI_VENDOR_ID);
    dev->device_id = pci_read16(dev, PCI_DEVICE_ID);
    dev->revision = pci_read8(dev, PCI_REVISION);
    dev->prog_if = pci_read8(dev, PCI_PROG_IF);
    dev->subclass = pci_read8(dev, PCI_SUBCLASS);
    dev->class_code = pci_read8(dev, PCI_CLASS);
    dev->header_type = pci_read8(dev, PCI_HEADER_TYPE);
    dev->irq_line = pci_read8(dev, PCI_IRQ_LINE);
    dev->irq_pin = pci_read8(dev, PCI_IRQ_PIN);

    read_bars(dev);
    read_capabilities(dev);
}

uint32_t pci_init(void)
{
    device_count = 0;

    // check every device number on every bus. this finds the functions
    // behind bridges too, without walking the bridge tree.
    for (uint32_t bus = 0; bus < 256; bus++)
    {
        for (uint8_t device = 0; device < 32; device++)
        {
            uint32_t id = raw_read32((uint8_t)bus, device, 0, PCI_VENDOR_ID);
            if ((id & 0xFFFFu) == 0xFFFFu)
            {
                continue; // no device
            }

            uint8_t header = (uint8_t)(raw_read32((uint8_t)bus, device, 0, 0x0C) >> 16);
            uint8_t functions = (header & 0x80u) ? 8 : 1; // multi-function
            for (uint8_t function = 0; function < functions; function++)
            {
                uint32_t fid = raw_read32((uint8_t)bus, device, function, PCI_VENDOR_ID);
                if ((fid & 0xFFFFu) != 0xFFFFu)
                {
                    add_function((uint8_t)bus, device, function);
                }
            }
        }
    }

    KLOG_INFO("pci: %u functions", device_count);
    return device_count;
}

uint32_t pci_device_count(void)
{
    return device_count;
}

pci_device_t *pci_device_at(uint32_t index)
{
    return index < device_count ? &devices[index] : NULL;
}

pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if, uint32_t nth)
{
    for (uint32_t i = 0; i < device_count; i++)
    {
        pci_device_t *dev = &devices[i];
        if (dev->class_code == class_code && dev->subclass == subclass &&
            (prog_if < 0 || dev->prog_if == (uint8_t)prog_if))
        {
            if (nth == 0)
            {
                return dev;
            }
            nth--;
        }
    }
    return NULL;
}

const char *pci_class_name(const pci_device_t *dev)
{
    switch (dev->class_code)
    {
    case 0x01:
        switch (dev->subclass)
        {
        case 0x01: return "IDE controller";
        case 0x06: return "SATA controller";
        case 0x08: return "NVMe controller";
        default:   return "storage controller";
        }
    case 0x02: return "network controller";
    case 0x03: return "display controller";
    case 0x04: return "multimedia controller";
    case 0x06:
        switch (dev->subclass)
        {
        case 0x00: return "host bridge";
        case 0x01: return "ISA bridge";
        case 0x04: return "PCI bridge";
        default:   return "bridge";
        }
    case 0x0C:
        if (dev->subclass == 0x03)
        {
            return dev->prog_if == 0x30 ? "USB controller (xHCI)" : "USB controller";
        }
        if (dev->subclass == 0x05)
        {
            return "SMBus controller";
        }
        return "serial bus controller";
    default:
        return "device";
    }
}

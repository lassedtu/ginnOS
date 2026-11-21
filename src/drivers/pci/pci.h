#pragma once

/**
 * @file pci.h
 * @brief PCI bus enumeration through the legacy configuration ports.
 *
 * pci_init reads the configuration space of every function on every bus
 * (ports 0xCF8 and 0xCFC) and keeps a table of what it finds: the ids, the
 * class, the BARs with their sizes, the interrupt line and pin, and where
 * the MSI and MSI-X capabilities are. drivers find their device by class or
 * by vendor and device id.
 */

#include "common/stdint.h"

#define PCI_MAX_DEVICES 64
#define PCI_BAR_COUNT   6

// classes and subclasses the kernel looks for
#define PCI_CLASS_STORAGE   0x01
#define PCI_SUBCLASS_SATA   0x06 // with prog_if 0x01: AHCI
#define PCI_SUBCLASS_NVME   0x08
#define PCI_CLASS_BRIDGE    0x06
#define PCI_CLASS_SERIAL    0x0C
#define PCI_SUBCLASS_USB    0x03 // with prog_if 0x30: xHCI

// configuration space offsets
#define PCI_COMMAND 0x04
#define PCI_COMMAND_IO          0x0001
#define PCI_COMMAND_MEMORY      0x0002
#define PCI_COMMAND_BUS_MASTER  0x0004
#define PCI_COMMAND_INTX_OFF    0x0400

typedef struct
{
    uint64_t base; // the address (I/O port or memory), 0 if the BAR is not used
    uint64_t size; // bytes the BAR decodes
    bool is_io;    // an I/O port range (else memory)
    bool is_64;    // a 64-bit memory BAR (it uses the next BAR too)
    bool prefetchable;
} pci_bar_t;

typedef struct
{
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type; // bits 0-6: 0 = device, 1 = PCI bridge
    uint8_t irq_line;    // the legacy IRQ the firmware routed (0xFF: none)
    uint8_t irq_pin;     // 1 = INTA ... 4 = INTD, 0 = none
    uint8_t msi_offset;  // the MSI capability in config space, 0 if none
    uint8_t msix_offset; // the MSI-X capability in config space, 0 if none
    pci_bar_t bars[PCI_BAR_COUNT];
} pci_device_t;

/**
 * enumerate the PCI bus. call once, before the drivers that use PCI.
 * @return the number of functions found.
 */
uint32_t pci_init(void);

/**
 * the number of functions pci_init found.
 */
uint32_t pci_device_count(void);

/**
 * a function by its index in the table.
 * @return the function, or NULL if index is out of range.
 */
pci_device_t *pci_device_at(uint32_t index);

/**
 * the nth function of a class, subclass and programming interface.
 * @param prog_if the programming interface, or -1 for any.
 * @param nth 0 for the first match.
 * @return the function, or NULL if there is no such match.
 */
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if, uint32_t nth);

/**
 * read or write the configuration space of a function (offsets are
 * aligned to the width).
 */
uint32_t pci_read32(const pci_device_t *dev, uint8_t offset);
uint16_t pci_read16(const pci_device_t *dev, uint8_t offset);
uint8_t pci_read8(const pci_device_t *dev, uint8_t offset);
void pci_write32(const pci_device_t *dev, uint8_t offset, uint32_t value);
void pci_write16(const pci_device_t *dev, uint8_t offset, uint16_t value);

/**
 * turn on memory decoding and bus mastering (DMA) for a function.
 */
void pci_enable_bus_master(const pci_device_t *dev);

/**
 * a short name for a class, for /proc/pci (for example "SATA controller").
 */
const char *pci_class_name(const pci_device_t *dev);

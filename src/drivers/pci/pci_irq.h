#pragma once

/**
 * @file pci_irq.h
 * @brief interrupts for PCI devices: MSI-X, MSI, or the legacy INTx line.
 *
 * a driver attaches one handler to its device. pci_irq_attach uses the
 * best way the device and the CPU support: MSI-X (entry 0), then MSI (one
 * message), then the interrupt line the firmware routed (INTx, shared,
 * level-triggered). MSI and MSI-X need the local APIC; with the 8259 PICs
 * only INTx is possible.
 *
 * limits: without an AML interpreter the kernel cannot read the ACPI _PRT,
 * so INTx uses the ISA line from the configuration space (irq_line). this
 * works with the 8259 PICs and on QEMU with the I/O APIC, but on real
 * machines with the I/O APIC a device without MSI can stay without
 * interrupts. drivers must keep a polled path for this case.
 */

#include "common/stdint.h"
#include "arch/arch_irq.h"
#include "drivers/pci/pci.h"

typedef enum
{
    PCI_IRQ_NONE, // no interrupt: the driver polls
    PCI_IRQ_INTX, // the legacy interrupt line
    PCI_IRQ_MSI,
    PCI_IRQ_MSIX,
} pci_irq_mode_t;

// how many device handlers the kernel keeps
#define PCI_IRQ_MAX 16

/**
 * attach an interrupt handler to a device and enable its interrupt.
 * call it once per device, before the device is told to raise interrupts.
 * @param dev the device.
 * @param handler the function to call; it must clear the cause in the
 *        device, and for INTx it must return quickly when its device has
 *        nothing to report (the line can be shared).
 * @param context passed to the handler.
 * @param name a short name for /proc/interrupts.
 * @return the mode in use, or PCI_IRQ_NONE if no interrupt is possible.
 */
pci_irq_mode_t pci_irq_attach(pci_device_t *dev, device_irq_handler_t handler, void *context,
                              const char *name);

/**
 * one attached handler, for /proc/interrupts.
 */
typedef struct
{
    const char *name;
    pci_irq_mode_t mode;
    uint32_t number; // the vector (MSI, MSI-X) or the IRQ line (INTx)
    uint32_t count;  // interrupts handled
} pci_irq_info_t;

/**
 * the attached handler at an index.
 * @return false if index is out of range.
 */
bool pci_irq_info(uint32_t index, pci_irq_info_t *info);

/**
 * a short name for a mode ("MSI-X", "MSI", "INTx", "none").
 */
const char *pci_irq_mode_name(pci_irq_mode_t mode);

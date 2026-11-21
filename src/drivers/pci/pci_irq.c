/**
 * interrupts for PCI devices (PCI Local Bus 3.0, section 6.8: MSI and
 * MSI-X).
 *
 * MSI: the capability holds one message address and data; the device
 *   writes the data to the address to raise the interrupt.
 * MSI-X: the capability points to a table in a BAR, with one 16-byte entry
 *   (address, data, vector control) per message. the driver uses entry 0.
 * INTx: the device asserts its interrupt pin until the cause is cleared.
 *   the line is shared, so each handler on it is called on each interrupt.
 *
 * every handler goes through dispatch(), which counts the interrupts for
 * /proc/interrupts.
 */

#include "pci_irq.h"

#include "kernel/irq/irq.h"
#include "kernel/klog/klog.h"
#include "kernel/memory/mmio.h"

// MSI capability (offsets from the capability)
#define MSI_CONTROL      0x02
#define MSI_ADDRESS_LOW  0x04
#define MSI_ADDRESS_HIGH 0x08 // 64-bit capable only
#define MSI_DATA_32      0x08
#define MSI_DATA_64      0x0C

#define MSI_CONTROL_ENABLE   0x0001u
#define MSI_CONTROL_MME_MASK 0x0070u // multiple message enable: 0 = one message
#define MSI_CONTROL_64BIT    0x0080u

// MSI-X capability
#define MSIX_CONTROL 0x02
#define MSIX_TABLE   0x04

#define MSIX_CONTROL_ENABLE    0x8000u
#define MSIX_CONTROL_FUNC_MASK 0x4000u
#define MSIX_TABLE_SIZE(c)     (((c) & 0x07FFu) + 1)
#define MSIX_ENTRY_SIZE        16u
#define MSIX_VECTOR_MASKED     0x1u

typedef struct
{
    device_irq_handler_t handler;
    void *context;
    const char *name;
    pci_irq_mode_t mode;
    uint32_t number;
    volatile uint32_t count;
} record_t;

static record_t records[PCI_IRQ_MAX];
static uint32_t record_count;

// the INTx lines that have the shared dispatcher (one irq_request each)
static uint16_t intx_lines;

static void dispatch(void *context)
{
    record_t *record = context;
    record->count++;
    record->handler(record->context);
}

/**
 * the dispatcher of an INTx line: every handler on the line runs.
 */
static void intx_dispatch(uint32_t irq, trap_frame_t *frame)
{
    (void)frame;
    for (uint32_t i = 0; i < record_count; i++)
    {
        if (records[i].mode == PCI_IRQ_INTX && records[i].number == irq)
        {
            dispatch(&records[i]);
        }
    }
}

static void set_intx_off(const pci_device_t *dev, bool off)
{
    uint16_t command = pci_read16(dev, PCI_COMMAND);
    command = off ? (command | PCI_COMMAND_INTX_OFF) : (command & ~PCI_COMMAND_INTX_OFF);
    pci_write16(dev, PCI_COMMAND, command);
}

/**
 * program entry 0 of the MSI-X table and enable MSI-X.
 */
static bool setup_msix(pci_device_t *dev, record_t *record)
{
    uint8_t cap = dev->msix_offset;
    uint16_t control = pci_read16(dev, cap + MSIX_CONTROL);
    uint32_t table = pci_read32(dev, cap + MSIX_TABLE);
    uint32_t bir = table & 0x7u;
    uint32_t offset = table & ~0x7u;

    if (bir >= PCI_BAR_COUNT || dev->bars[bir].base == 0 || dev->bars[bir].is_io)
    {
        return false;
    }
    if (sizeof(phys_addr_t) < 8 && (dev->bars[bir].base >> 32) != 0)
    {
        return false;
    }

    uint64_t address;
    uint32_t data;
    if (!arch_msi_alloc(dispatch, record, &address, &data))
    {
        return false;
    }

    size_t bytes = MSIX_TABLE_SIZE(control) * MSIX_ENTRY_SIZE;
    volatile uint32_t *entry =
        ioremap_nocache((phys_addr_t)(dev->bars[bir].base + offset), bytes);
    if (!entry)
    {
        return false;
    }

    // mask the whole function while the table changes
    pci_write16(dev, cap + MSIX_CONTROL, control | MSIX_CONTROL_ENABLE | MSIX_CONTROL_FUNC_MASK);
    mmio_write32(&entry[0], (uint32_t)address);
    mmio_write32(&entry[1], (uint32_t)(address >> 32));
    mmio_write32(&entry[2], data);
    mmio_write32(&entry[3], mmio_read32(&entry[3]) & ~MSIX_VECTOR_MASKED);
    // the other entries stay masked (their reset state)
    pci_write16(dev, cap + MSIX_CONTROL,
                (uint16_t)((control | MSIX_CONTROL_ENABLE) & ~MSIX_CONTROL_FUNC_MASK));

    record->mode = PCI_IRQ_MSIX;
    record->number = data;
    return true;
}

/**
 * program one MSI message and enable MSI.
 */
static bool setup_msi(pci_device_t *dev, record_t *record)
{
    uint8_t cap = dev->msi_offset;
    uint64_t address;
    uint32_t data;
    if (!arch_msi_alloc(dispatch, record, &address, &data))
    {
        return false;
    }

    uint16_t control = pci_read16(dev, cap + MSI_CONTROL);
    pci_write32(dev, cap + MSI_ADDRESS_LOW, (uint32_t)address);
    if (control & MSI_CONTROL_64BIT)
    {
        pci_write32(dev, cap + MSI_ADDRESS_HIGH, (uint32_t)(address >> 32));
        pci_write16(dev, cap + MSI_DATA_64, (uint16_t)data);
    }
    else
    {
        pci_write16(dev, cap + MSI_DATA_32, (uint16_t)data);
    }
    control = (uint16_t)((control & ~MSI_CONTROL_MME_MASK) | MSI_CONTROL_ENABLE);
    pci_write16(dev, cap + MSI_CONTROL, control);

    record->mode = PCI_IRQ_MSI;
    record->number = data;
    return true;
}

/**
 * use the interrupt line the firmware routed.
 */
static bool setup_intx(pci_device_t *dev, record_t *record)
{
    if (dev->irq_pin == 0 || dev->irq_line >= 16)
    {
        return false;
    }

    uint32_t line = dev->irq_line;
    record->mode = PCI_IRQ_INTX;
    record->number = line;

    if (!(intx_lines & (1u << line)))
    {
        // PCI interrupt lines are level-triggered
        arch_irq_set_level(line);
        if (!irq_request(line, intx_dispatch, IRQ_FLAG_NONE, "pci"))
        {
            record->mode = PCI_IRQ_NONE;
            return false;
        }
        intx_lines |= (uint16_t)(1u << line);
    }
    set_intx_off(dev, false);
    return true;
}

pci_irq_mode_t pci_irq_attach(pci_device_t *dev, device_irq_handler_t handler, void *context,
                              const char *name)
{
    if (!dev || !handler || record_count == PCI_IRQ_MAX)
    {
        return PCI_IRQ_NONE;
    }

    record_t *record = &records[record_count];
    record->handler = handler;
    record->context = context;
    record->name = name;
    record->mode = PCI_IRQ_NONE;
    record->count = 0;

    // the record must be visible to the INTx dispatcher before the line
    // can fire
    record_count++;

    bool message = (dev->msix_offset && setup_msix(dev, record)) ||
                   (dev->msi_offset && setup_msi(dev, record));
    if (message)
    {
        // with a message, the INTx pin must not fire as well
        set_intx_off(dev, true);
    }
    else if (!setup_intx(dev, record))
    {
        record_count--;
        KLOG_WARN("pci: %s: no interrupt; the driver polls", name);
        return PCI_IRQ_NONE;
    }

    KLOG_INFO("pci: %s: %s %u", name, pci_irq_mode_name(record->mode), record->number);
    return record->mode;
}

bool pci_irq_info(uint32_t index, pci_irq_info_t *info)
{
    if (index >= record_count)
    {
        return false;
    }
    info->name = records[index].name;
    info->mode = records[index].mode;
    info->number = records[index].number;
    info->count = records[index].count;
    return true;
}

const char *pci_irq_mode_name(pci_irq_mode_t mode)
{
    switch (mode)
    {
    case PCI_IRQ_MSIX:
        return "MSI-X";
    case PCI_IRQ_MSI:
        return "MSI";
    case PCI_IRQ_INTX:
        return "INTx";
    default:
        return "none";
    }
}

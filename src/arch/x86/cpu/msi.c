/**
 * message-signalled interrupts on x86 (Intel SDM volume 3, section 11.11).
 *
 * a device raises an MSI by a memory write: the address selects the local
 * APIC (0xFEE00000 with the APIC id in bits 12 to 19), and the data is the
 * vector (fixed delivery, edge). each allocation takes one vector from the
 * device range below. the vector handler sends the end of interrupt first,
 * like the IRQ path does, and then calls the device handler.
 */

#include "arch/arch_irq.h"

#include "apic.h"
#include "isr.h"
#include "irq.h"

// the vectors for devices: after the ISA IRQs (0x20 to 0x2F), below the
// syscall gate (0x80)
#define DEVICE_VECTOR_FIRST 0x30
#define DEVICE_VECTOR_LAST  0x7F
#define DEVICE_VECTOR_COUNT (DEVICE_VECTOR_LAST - DEVICE_VECTOR_FIRST + 1)

#define MSI_ADDRESS_BASE 0xFEE00000u

static struct
{
    device_irq_handler_t handler;
    void *context;
} vectors[DEVICE_VECTOR_COUNT];

/**
 * the handler of every device vector.
 */
static void msi_handler(struct registers *regs)
{
    apic_eoi();

    uint32_t index = (uint32_t)regs->interrupt - DEVICE_VECTOR_FIRST;
    if (index < DEVICE_VECTOR_COUNT && vectors[index].handler)
    {
        vectors[index].handler(vectors[index].context);
    }
}

bool arch_msi_alloc(device_irq_handler_t handler, void *context, uint64_t *address, uint32_t *data)
{
    if (!apic_active() || !handler)
    {
        return false;
    }

    for (uint32_t i = 0; i < DEVICE_VECTOR_COUNT; i++)
    {
        if (vectors[i].handler)
        {
            continue;
        }

        vectors[i].handler = handler;
        vectors[i].context = context;
        isr_register_handler((int)(DEVICE_VECTOR_FIRST + i), msi_handler);

        *address = MSI_ADDRESS_BASE | ((uint32_t)apic_id() << 12);
        *data = DEVICE_VECTOR_FIRST + i;
        return true;
    }
    return false;
}

void arch_irq_set_level(uint32_t irq)
{
    if (apic_active())
    {
        apic_set_irq_level(irq);
    }
}

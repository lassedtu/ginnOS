#include "arch/arch_irq.h"

#include "arch/x86/cpu/irq.h"
#include "arch/x86/cpu/isr.h"
#include "arch/x86/cpu/pic.h"
#include "arch/x86/cpu/apic.h"

/**
 * @file arch_irq_x86.c
 * @brief x86 implementation of the arch-neutral IRQ API.
 *
 * wraps the existing PIC/IRQ machinery. neutral handlers are kept in a table
 * indexed by IRQ line; a single adapter registered with the x86 IRQ layer
 * recovers the line number from the saved register state and forwards to the
 * neutral handler. this keeps irq.c's dispatch, spurious-IRQ handling, and
 * EOI logic untouched while giving portable drivers a stable interface.
 *
 * on x86, trap_frame_t is the ISR register dump (struct registers); the cast
 * lives here because this is the only place that legitimately knows the layout.
 */

#define IRQ_LINE_COUNT 16

// neutral handlers registered by portable drivers, one per IRQ line.
static irq_handler_fn neutral_handlers[IRQ_LINE_COUNT];

// the lines the IRQ layer has enabled, so apic_init can enable them again
static uint16_t enabled_lines;

/**
 * shared x86-side adapter. runs in the x86 IRQ dispatch path, recovers the
 * IRQ line from the register dump, and forwards to the neutral handler.
 * @param regs saved register state (carries the interrupt vector).
 */
static void irq_adapter(struct registers *regs)
{
    uint32_t irq = (uint32_t)regs->interrupt - PIC_REMAP_OFFSET;
    if (irq < IRQ_LINE_COUNT && neutral_handlers[irq] != 0)
    {
        neutral_handlers[irq](irq, (trap_frame_t *)regs);
    }
}

int arch_irq_register(uint32_t irq, irq_handler_fn handler)
{
    if (irq >= IRQ_LINE_COUNT)
    {
        return -1;
    }

    neutral_handlers[irq] = handler;
    irq_register_handler((int)irq, irq_adapter);
    return 0;
}

void arch_irq_enable(uint32_t irq)
{
    if (irq < IRQ_LINE_COUNT)
    {
        enabled_lines |= (uint16_t)(1u << irq);
    }
    if (apic_active())
    {
        apic_set_irq_masked(irq, false);
    }
    else
    {
        pic_unmask((int)irq);
    }
}

void arch_irq_disable(uint32_t irq)
{
    if (irq < IRQ_LINE_COUNT)
    {
        enabled_lines &= (uint16_t)~(1u << irq);
    }
    if (apic_active())
    {
        apic_set_irq_masked(irq, true);
    }
    else
    {
        pic_mask((int)irq);
    }
}

void arch_irq_ack(uint32_t irq)
{
    if (apic_active())
    {
        apic_eoi();
    }
    else
    {
        pic_send_eoi((int)irq);
    }
}

bool irq_line_wanted(uint32_t irq)
{
    return irq < IRQ_LINE_COUNT && (enabled_lines & (1u << irq)) != 0;
}

bool arch_frame_is_user(const trap_frame_t *frame)
{
    const struct registers *regs = (const struct registers *)frame;
    return regs && (regs->cs & 3u) == 3u;
}

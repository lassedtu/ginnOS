#include "irq.h"
#include "pic.h"
#include "io.h"
#include "isr.h"
#include "../../../common/stdio.h"
#include "../../../kernel/assert.h"

/** PIC remap offset. IRQ 0 maps to IDT vector 0x20 (32). */
#define PIC_REMAP_OFFSET 0x20

/** handler table for the 16 hardware IRQ lines (IRQ 0–15). */
static irq_handler_t irq_handlers[16];

/**
 * ISR-level handler for hardware IRQs.
 * translates the interrupt vector back to an IRQ number, dispatches
 * to a registered handler if one exists, and sends EOI to the PIC.
 * @param regs pointer to the saved register state.
 */
static void irq_handler(struct registers *regs)
{
    int irq = regs->interrupt - PIC_REMAP_OFFSET;

    // if the IRQ number is out of range, send EOI anyway so the PIC line is
    // released, then bail — skipping EOI here would permanently freeze all
    // lower-priority interrupts since the PIC would never see the line cleared.
    if (irq < 0 || irq >= 16)
    {
        pic_send_eoi(regs->interrupt - PIC_REMAP_OFFSET);
        return;
    }

    // spurious IRQ check for IRQ 7 (master PIC).
    // the 8259A can raise IRQ 7 without a real request; check the ISR to confirm.
    // if bit 7 of the master ISR is clear, this is spurious — do not send EOI.
    if (irq == 7)
    {
        uint16_t isr = pic_read_isr();
        if (!(isr & (1 << 7)))
        {
            return;
        }
    }

    // spurious IRQ check for IRQ 15 (slave PIC).
    // if bit 7 of the slave ISR is clear, this is spurious.
    // the master still needs EOI because it saw a real cascade signal on IRQ 2.
    if (irq == 15)
    {
        uint16_t isr = pic_read_isr();
        if (!(isr & (1 << 15)))
        {
            pic_send_eoi(2); // EOI to master only for the cascade line
            return;
        }
    }

    if (irq_handlers[irq] != 0)
    {
        irq_handlers[irq](regs);
    }
#ifdef DEBUG_UNHANDLED_IRQS
    else
    {
        printf("Unhandled IRQ %d\r\n", irq);
    }
#endif

    // acknowledge the interrupt to the PIC
    pic_send_eoi(irq);
}

void irq_initialize(void)
{
    // remap PIC so IRQs 0–15 map to IDT vectors 0x20–0x2F
    pic_configure(PIC_REMAP_OFFSET, PIC_REMAP_OFFSET + 8);

    // register the common IRQ handler on each of the 16 IRQ vectors
    for (int i = 0; i < 16; i++)
    {
        isr_register_handler(PIC_REMAP_OFFSET + i, irq_handler);
    }
}

void irq_register_handler(int irq, irq_handler_t handler)
{
    ASSERT(irq >= 0 && irq <= 15);

    irq_handlers[irq] = handler;
}

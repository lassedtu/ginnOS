#include "isr.h"

#include "idt.h"
#include "../../../common/stdint.h"
#include "../../../common/stdio.h"
#include "../../../kernel/assert.h"

/**
 * Interrupt handler table.
 *
 * Each interrupt vector (0-255) can have a registered handler.
 */
static isr_handler_t handlers[256];

/**
 * Generated assembly function that installs all ISR gates.
 */
extern void isr_init_gates(void);

/**
 * Initialize the interrupt system.
 *
 * Installs all 256 ISR stubs into the IDT without marking any gate present.
 * A gate becomes present only when a handler is registered via
 * isr_register_handler(), making the present-bit meaningful rather than
 * a blanket "everything is active" flag.
 */
void isr_initialize(void)
{
    isr_init_gates();
}

/**
 * Common C interrupt dispatcher.
 *
 * Called from the assembly ISR stubs.
 */
void __attribute__((cdecl)) isr_handler(struct registers *regs)
{
    if (handlers[regs->interrupt])
    {
        handlers[regs->interrupt](regs);
    }
#ifdef DEBUG_UNHANDLED_IRQS
    else
    {
        printf("Unhandled interrupt %u\r\n", regs->interrupt);
    }
#endif
}

/**
 * Register a handler for an interrupt vector.
 */
void isr_register_handler(
    int vector,
    isr_handler_t handler)
{
    ASSERT(vector >= 0 && vector <= 255);

    handlers[vector] = handler;

    idt_enable_gate(vector);
}
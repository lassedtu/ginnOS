#include "isr.h"

#include "idt.h"
#include "../../../common/stdint.h"
#include "../../../common/stdio.h"
#include "../../../kernel/assert.h"

/**
 * interrupt handler table.
 *
 * live vector ranges (vectors become present only when a handler is registered
 * via isr_register_handler):
 *
 *   0–31   CPU exceptions — registered by exception_initialize()
 *  32–47   Hardware IRQs  — registered by irq_initialize() via PIC_REMAP_OFFSET
 *  48–255  Unassigned     — any new subsystem claiming a vector in this range
 *          MUST call isr_register_handler() before that vector can fire.
 *          Record the claim here: (none yet)
 */
static isr_handler_t handlers[256];

/**
 * one-bit-per-vector "already warned" table.
 * prevents a single unhandled vector from spamming the console on every
 * occurrence; each distinct unhandled vector is reported exactly once per boot.
 */
static uint32_t warned_vectors[256 / 32];

/**
 * generated assembly function that installs all ISR gates.
 */
extern void isr_init_gates(void);

/**
 * initialize the interrupt system.
 *
 * installs all 256 ISR stubs into the IDT without marking any gate present.
 * a gate becomes present only when a handler is registered via
 * isr_register_handler(), making the present-bit meaningful rather than
 * a blanket "everything is active" flag.
 */
void isr_initialize(void)
{
    isr_init_gates();
}

/**
 * common C interrupt dispatcher.
 *
 * called from the assembly ISR stubs.
 */
void __attribute__((cdecl)) isr_handler(struct registers *regs)
{
    if (handlers[regs->interrupt])
    {
        handlers[regs->interrupt](regs);
    }
    else
    {
        uint32_t vec = regs->interrupt;
        uint32_t word = vec / 32;
        uint32_t bit = 1u << (vec % 32);

        if (!(warned_vectors[word] & bit))
        {
            warned_vectors[word] |= bit;
            printf("Unhandled interrupt %u\r\n", vec);
        }
    }
}

/**
 * register a handler for an interrupt vector.
 */
void isr_register_handler(
    int vector,
    isr_handler_t handler)
{
    ASSERT(vector >= 0 && vector <= 255);

    handlers[vector] = handler;

    idt_enable_gate(vector);
}
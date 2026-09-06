#pragma once

/**
 * arch-neutral hardware-interrupt (IRQ) interface.
 *
 * portable drivers register their line handlers through this API instead of
 * reaching into the x86 PIC/IDT/ISR machinery. each architecture provides its
 * own implementation that wraps whatever interrupt controller it has (8259A
 * PIC on x86, GIC on ARM, PLIC on RISC-V).
 */

#include "common/stdint.h"

/**
 * opaque saved-state handle passed to an IRQ handler.
 *
 * the concrete layout is architecture-specific (on x86 it is the register
 * dump built by the ISR stubs). portable code treats it as opaque; only
 * arch code that knows the layout may dereference it. handlers that just
 * need to service a device can ignore it entirely.
 */
typedef struct trap_frame trap_frame_t;

/**
 * IRQ handler function type.
 * @param irq the hardware IRQ line that fired.
 * @param frame opaque saved state at the point of interruption (may be NULL
 *              to a handler that does not need it).
 */
typedef void (*irq_handler_fn)(uint32_t irq, trap_frame_t *frame);

/**
 * register a handler for a hardware IRQ line.
 * @param irq the IRQ line to handle.
 * @param handler the function to call when the line fires.
 * @return 0 on success, -1 if the IRQ number is out of range.
 */
int arch_irq_register(uint32_t irq, irq_handler_fn handler);

/**
 * unmask (enable) a hardware IRQ line so it can be delivered.
 * @param irq the IRQ line to enable.
 */
void arch_irq_enable(uint32_t irq);

/**
 * mask (disable) a hardware IRQ line.
 * @param irq the IRQ line to disable.
 */
void arch_irq_disable(uint32_t irq);

/**
 * acknowledge an IRQ (end-of-interrupt) at the interrupt controller.
 * @param irq the IRQ line to acknowledge.
 */
void arch_irq_ack(uint32_t irq);

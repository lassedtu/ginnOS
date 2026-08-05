#pragma once

#include "isr.h"

/**
 * IRQ handler function type.
 * @param regs pointer to the saved register state at the time of the interrupt.
 */
typedef void (*irq_handler_t)(struct registers *regs);

/**
 * initialize the IRQ subsystem.
 * configures the PIC, registers ISR handlers for the 16 hardware IRQ lines,
 * and enables hardware interrupts.
 */
void irq_initialize(void);

/**
 * register a handler function for a specific hardware IRQ line.
 * @param irq the IRQ number (0–15).
 * @param handler pointer to the handler function to call when this IRQ fires.
 */
void irq_register_handler(int irq, irq_handler_t handler);

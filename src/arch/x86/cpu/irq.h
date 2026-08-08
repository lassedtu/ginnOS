#pragma once

#include "isr.h"

/**
 * IDT vector at which IRQ 0 is mapped.
 * IRQs 0–15 occupy vectors PIC_REMAP_OFFSET through PIC_REMAP_OFFSET+15.
 * Must stay above the CPU exception range (0–31) and must match the value
 * passed to pic_configure() in irq_initialize().
 */
#define PIC_REMAP_OFFSET 0x20

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

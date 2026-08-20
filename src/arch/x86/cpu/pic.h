#pragma once

#include "common/stdint.h"

/**
 * configure the 8259A PIC.
 * remaps IRQ lines to the specified IDT vector offsets.
 * @param offset_master base vector offset for the master PIC (IRQ 0–7).
 * @param offset_slave base vector offset for the slave PIC (IRQ 8–15).
 */
void pic_configure(uint8_t offset_master, uint8_t offset_slave);

/**
 * send an End-of-Interrupt (EOI) signal to the PIC.
 * if the IRQ came from the slave PIC, sends EOI to both slave and master.
 * @param irq the IRQ number (0–15).
 */
void pic_send_eoi(int irq);

/**
 * disable both PICs by masking all IRQ lines.
 */
void pic_disable(void);

/**
 * mask (disable) a specific IRQ line.
 * @param irq the IRQ number (0–15).
 */
void pic_mask(int irq);

/**
 * unmask (enable) a specific IRQ line.
 * @param irq the IRQ number (0–15).
 */
void pic_unmask(int irq);

/**
 * read the Interrupt Request Register (IRR) from both PICs.
 * @return 16-bit value with master in low byte, slave in high byte.
 */
uint16_t pic_read_irr(void);

/**
 * read the In-Service Register (ISR) from both PICs.
 * @return 16-bit value with master in low byte, slave in high byte.
 */
uint16_t pic_read_isr(void);

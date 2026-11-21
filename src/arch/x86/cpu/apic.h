#pragma once

/**
 * @file apic.h
 * @brief the local APIC and the I/O APIC.
 *
 * the kernel starts with the 8259 PICs. apic_init changes to the APICs if
 * the ACPI MADT describes them: the 8259s are masked, the I/O APIC sends
 * the ISA IRQs to the same vectors as before (PIC_REMAP_OFFSET + irq), and
 * the local APIC timer gives the scheduler tick on the vector of IRQ 0.
 * without a MADT the kernel stays on the 8259s.
 */

#include "common/stdint.h"

/**
 * change from the 8259 PICs to the APICs. call with interrupts off, after
 * acpi_init (it needs the MADT) and mmio_init.
 * @param timer_hz the tick rate for the local APIC timer.
 * @return true if the APICs are in use.
 */
bool apic_init(uint32_t timer_hz);

/**
 * true after a successful apic_init.
 */
bool apic_active(void);

/**
 * end of interrupt to the local APIC.
 */
void apic_eoi(void);

/**
 * mask or unmask the I/O APIC entry of an ISA IRQ (0 to 15).
 */
void apic_set_irq_masked(uint32_t irq, bool masked);

/**
 * the id of the local APIC, for the destination of MSI messages.
 */
uint8_t apic_id(void);

/**
 * make the I/O APIC entry of an ISA IRQ level-triggered (a PCI interrupt
 * line routed to it). the polarity stays as the MADT gives it.
 */
void apic_set_irq_level(uint32_t irq);

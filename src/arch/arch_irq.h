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
 * true if the interrupt came while user code ran (CPU time accounting).
 * @param frame the frame a handler was given.
 */
bool arch_frame_is_user(const trap_frame_t *frame);

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

/**
 * a device interrupt handler (a message-signalled interrupt, or a PCI
 * interrupt line). it runs with interrupts off, after the end of interrupt
 * is sent, so it must clear the cause in the device.
 * @param context the value given when the handler was attached.
 */
typedef void (*device_irq_handler_t)(void *context);

/**
 * allocate an interrupt vector for a message-signalled interrupt (PCI MSI
 * or MSI-X) and give the message that the device must write to raise it.
 * @param handler the function to call when the message arrives.
 * @param context passed to the handler.
 * @param address receives the message address.
 * @param data receives the message data.
 * @return false if the CPU cannot take messages (no local APIC in use) or
 *         all device vectors are in use.
 */
bool arch_msi_alloc(device_irq_handler_t handler, void *context, uint64_t *address, uint32_t *data);

/**
 * make an ISA IRQ line level-triggered, for a PCI interrupt line that the
 * firmware routed to it. does nothing when the line is already level or
 * when the 8259 PICs are in use (the firmware sets their trigger mode).
 * @param irq the IRQ line (0 to 15).
 */
void arch_irq_set_level(uint32_t irq);

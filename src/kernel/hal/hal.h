#pragma once

/**
 * initialize the hardware abstraction layer.
 * brings up the arch's interrupt and device machinery in the correct order
 * (on x86: GDT, IDT, ISR, exceptions, PIC/IRQ, then the timer and keyboard).
 * interrupts are left disabled; the caller enables them once every handler
 * is in place. each architecture provides its own implementation.
 */
void hal_initialize(void);

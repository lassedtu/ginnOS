#pragma once

/**
 * initialize the hardware abstraction layer.
 * sets up the GDT, IDT, ISR, and IRQ subsystems in the correct order.
 * after this call, hardware interrupts are enabled and the interrupt
 * subsystem is fully operational.
 */
void hal_initialize(void);

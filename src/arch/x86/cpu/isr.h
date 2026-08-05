#pragma once

#include "../../../common/stdint.h"

/**
 * saved register state pushed onto the stack by ISR stubs.
 * this structure matches the layout created by the assembly ISR common handler:
 *   1. data segment (pushed by us)
 *   2. general-purpose registers (pushed by pusha)
 *   3. interrupt number and error code (pushed by stub / CPU)
 *   4. eip, cs, eflags, esp, ss (pushed automatically by the CPU)
 */
struct registers
{
    uint32_t ds;                                           // data segment selector.
    uint32_t edi, esi, ebp, kern_esp, ebx, edx, ecx, eax; // general-purpose registers from pusha.
    uint32_t interrupt;                                    // interrupt vector number.
    uint32_t error;                                        // error code (or zero for interrupts without one).
    uint32_t eip, cs, eflags, esp, ss;                     // pushed automatically by the CPU on interrupt.
} __attribute__((packed));

/**
 * interrupt service routine handler function type.
 * @param regs pointer to the saved register state at the time of the interrupt.
 */
typedef void (*isr_handler_t)(struct registers *regs);

/**
 * initialize the interrupt service routine subsystem.
 * installs all 256 ISR gates into the IDT and enables them.
 */
void isr_initialize(void);

/**
 * register a handler function for a specific interrupt vector.
 * @param vector interrupt vector number (0–255).
 * @param handler pointer to the handler function to call when this interrupt fires.
 */
void isr_register_handler(int vector, isr_handler_t handler);

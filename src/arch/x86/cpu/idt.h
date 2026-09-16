#pragma once

#include "common/stdint.h"

/**
 * IDT gate type and attribute flags.
 * combine these with bitwise OR to build the flags byte for an IDT entry.
 */
enum idt_flags
{
    IDT_FLAG_GATE_TASK = 0x05,       // task gate.
    IDT_FLAG_GATE_16BIT_INT = 0x06,  // 16-bit interrupt gate.
    IDT_FLAG_GATE_16BIT_TRAP = 0x07, // 16-bit trap gate.
    IDT_FLAG_GATE_32BIT_INT = 0x0E,  // 32-bit interrupt gate.
    IDT_FLAG_GATE_32BIT_TRAP = 0x0F, // 32-bit trap gate.

    IDT_FLAG_RING0 = (0 << 5), // descriptor privilege level 0 (kernel).
    IDT_FLAG_RING1 = (1 << 5), // descriptor privilege level 1.
    IDT_FLAG_RING2 = (2 << 5), // descriptor privilege level 2.
    IDT_FLAG_RING3 = (3 << 5), // descriptor privilege level 3 (user).

    IDT_FLAG_PRESENT = 0x80 // segment present flag.
};

/**
 * initialize the Interrupt Descriptor Table (IDT) and load it into the IDTR register.
 */
void idt_initialize(void);

/**
 * configure a single IDT gate entry.
 * @param vector interrupt vector number (0–255).
 * @param base pointer to the interrupt service routine.
 * @param segment code segment selector for the ISR.
 * @param flags gate type and attribute flags (see idt_flags).
 */
void idt_set_gate(int vector, void *base, uint16_t segment, uint8_t flags);

/**
 * enable an IDT gate by setting its present bit.
 * @param vector interrupt vector number (0–255).
 */
void idt_enable_gate(int vector);

/**
 * disable an IDT gate by clearing its present bit.
 * @param vector interrupt vector number (0–255).
 */
void idt_disable_gate(int vector);

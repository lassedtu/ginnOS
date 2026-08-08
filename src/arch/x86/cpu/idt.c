#include "idt.h"
#include "../../../common/stdint.h"
#include "../../../kernel/assert.h"

/**
 * IDT entry structure.
 * each entry describes a single interrupt gate, trap gate, or task gate.
 */
struct idt_entry
{
    uint16_t base_low;         // lower 16 bits of the ISR address.
    uint16_t segment_selector; // GDT segment selector for the ISR code segment.
    uint8_t reserved;          // must be zero.
    uint8_t flags;             // gate type, DPL, and present bit.
    uint16_t base_high;        // upper 16 bits of the ISR address.
} __attribute__((packed));

/**
 * IDT descriptor structure (pointer loaded by LIDT).
 */
struct idt_ptr
{
    uint16_t limit; // size of the IDT in bytes minus 1.
    uint32_t base;  // linear address of the first IDT entry.
} __attribute__((packed));

/** defined in idt_load.asm, loads the IDT descriptor into the IDTR register. */
extern void idt_load(uint32_t idt_ptr_addr);

/** the 256-entry IDT. */
static struct idt_entry idt[256];

/** IDT descriptor used by the LIDT instruction. */
static struct idt_ptr ip;

void idt_set_gate(int vector, void *base, uint16_t segment, uint8_t flags)
{
    ASSERT(vector >= 0 && vector <= 255);

    idt[vector].base_low =
        ((uint32_t)base) & 0xFFFF;

    idt[vector].segment_selector = segment;

    idt[vector].reserved = 0;

    idt[vector].flags = flags;

    idt[vector].base_high =
        ((uint32_t)base >> 16) & 0xFFFF;
}

void idt_enable_gate(int vector)
{
    idt[vector].flags |= IDT_FLAG_PRESENT;
}

void idt_disable_gate(int vector)
{
    idt[vector].flags &= ~IDT_FLAG_PRESENT;
}

void idt_initialize(void)
{
    ip.limit =
        sizeof(idt) - 1;

    ip.base =
        (uint32_t)&idt;

    // load the IDT descriptor into the IDTR register
    idt_load((uint32_t)&ip);
}

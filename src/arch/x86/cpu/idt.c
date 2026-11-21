#include "idt.h"
#include "common/stdint.h"
#include "kernel/assert.h"

#ifdef __x86_64__

/**
 * IDT entry structure (x86_64): a 16-byte gate. the handler address is in
 * three parts.
 */
struct idt_entry
{
    uint16_t base_low;         // bits 0-15 of the ISR address.
    uint16_t segment_selector; // GDT segment selector for the ISR code segment.
    uint8_t ist;               // bits 0-2: the IST stack (0: no stack switch).
    uint8_t flags;             // gate type, DPL, and present bit.
    uint16_t base_middle;      // bits 16-31 of the ISR address.
    uint32_t base_high;        // bits 32-63 of the ISR address.
    uint32_t reserved;         // must be zero.
} __attribute__((packed));

/**
 * IDT descriptor structure (pointer loaded by LIDT).
 */
struct idt_ptr
{
    uint16_t limit; // size of the IDT in bytes minus 1.
    uint64_t base;  // linear address of the first IDT entry.
} __attribute__((packed));

#else

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

#endif

/** the 256-entry IDT. */
static struct idt_entry idt[256];

/** IDT descriptor used by the LIDT instruction. */
static struct idt_ptr ip;

void idt_set_gate(int vector, void *base, uint16_t segment, uint8_t flags)
{
    ASSERT(vector >= 0 && vector <= 255);

    uintptr_t address = (uintptr_t)base;

    idt[vector].base_low = (uint16_t)(address & 0xFFFF);
    idt[vector].segment_selector = segment;
    idt[vector].flags = flags;
#ifdef __x86_64__
    idt[vector].ist = 0;
    idt[vector].base_middle = (uint16_t)((address >> 16) & 0xFFFF);
    idt[vector].base_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
#else
    idt[vector].reserved = 0;
    idt[vector].base_high = (uint16_t)((address >> 16) & 0xFFFF);
#endif
}

#ifdef __x86_64__
void idt_set_ist(int vector, uint8_t ist)
{
    ASSERT(vector >= 0 && vector <= 255);
    idt[vector].ist = ist & 7u;
}
#endif

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
    ip.limit = sizeof(idt) - 1;
    ip.base = (uintptr_t)&idt;

    // load the IDT descriptor into the IDTR register
#ifdef __x86_64__
    __asm__ volatile("lidt %0" : : "m"(ip));
#else
    idt_load((uint32_t)(uintptr_t)&ip);
#endif
}

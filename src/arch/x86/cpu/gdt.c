#include "gdt.h"
#include "../../../common/stdint.h"

/**
 * Global Descriptor Table (GDT) entry structure.
 */
struct gdt_entry
{
    uint16_t limit_low;  // lower 16 bits of the segment limit.
    uint16_t base_low;   // lower 16 bits of the base address.
    uint8_t base_middle; // next 8 bits of the base address.
    uint8_t access;      // access flags (type, descriptor type, privilege level, present bit).
    uint8_t granularity; // granularity flags (segment limit, size, granularity).
    uint8_t base_high;   // last 8 bits of the base address.
} __attribute__((packed));

/**
 * Global Descriptor Table (GDT) pointer structure.
 */
struct gdt_ptr
{
    uint16_t limit; // size of the GDT in bytes minus 1.
    uint32_t base;  // linear address of the first GDT entry.
} __attribute__((packed));

extern void gdt_flush(uint32_t); // flushes the GDT by loading the new GDT pointer into the GDTR register.

static struct gdt_entry gdt[3]; // GDT with 3 entries: null, kernel code segment, kernel data segment.
static struct gdt_ptr gp;       // GDT pointer structure used to load the GDT into the GDTR register.

/**
 * set a GDT entry at the specified index with the given base, limit, access flags, and granularity.
 * @param index index of the GDT entry to set.
 * @param base base address of the segment.
 * @param limit limit of the segment.
 * @param access access flags for the segment.
 * @param granularity granularity flags for the segment.
 */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity)
{
    // set base address of the segment in the GDT entry
    gdt[index].base_low =
        base & 0xFFFF;

    // set middle and high parts of the base address
    gdt[index].base_middle =
        (base >> 16) & 0xFF;

    // set high part of the base address
    gdt[index].base_high =
        (base >> 24) & 0xFF;

    // set segment limit in the GDT entry
    gdt[index].limit_low =
        limit & 0xFFFF;

    // set granularity and high bits of the segment limit
    gdt[index].granularity =
        (limit >> 16) & 0x0F;

    // set granularity flags (size, granularity) in the GDT entry
    gdt[index].granularity |=
        granularity & 0xF0;

    // set access flags (type, descriptor type, privilege level, present bit) in the GDT entry
    gdt[index].access = access;
}

void gdt_initialize(void)
{
    gp.limit =
        sizeof(gdt) - 1;

    gp.base =
        (uint32_t)&gdt;

    // null descriptor
    gdt_set_entry(
        0,
        0,
        0,
        0,
        0);

    // kernel code segment
    gdt_set_entry(
        1,
        0,
        0xFFFFFFFF,
        0x9A,
        0xCF);

    // kernel data segment
    gdt_set_entry(
        2,
        0,
        0xFFFFFFFF,
        0x92,
        0xCF);

    // load the new GDT into the GDTR register
    gdt_flush((uint32_t)&gp);
}
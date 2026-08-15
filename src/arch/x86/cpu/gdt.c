#include "gdt.h"
#include "../../../common/memory.h"

struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/**
 * x86 Task State Segment.
 * only SS0 and ESP0 are used; the rest is zeroed but must be present
 * for the CPU to accept the TSS as valid.
 */
struct tss_entry
{
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

extern void gdt_flush(uint32_t);
extern void tss_flush(void);

static struct gdt_entry gdt[6];
static struct gdt_ptr gp;
static struct tss_entry tss;

static void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity)
{
    gdt[index].base_low = base & 0xFFFF;
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;

    gdt[index].limit_low = limit & 0xFFFF;
    gdt[index].granularity = (limit >> 16) & 0x0F;
    gdt[index].granularity |= granularity & 0xF0;

    gdt[index].access = access;
}

static void tss_install(int index)
{
    uint32_t base = (uint32_t)&tss;
    uint32_t limit = sizeof(tss) - 1;

    memset(&tss, 0, sizeof(tss));

    tss.ss0 = GDT_KERNEL_DATA;
    tss.esp0 = 0; /* set properly before entering ring 3 */
    tss.iomap_base = sizeof(tss);

    /* TSS descriptor: access byte = 0x89 (present, DPL 0, TSS type 9 = available 32-bit TSS) */
    gdt_set_entry(index, base, limit, 0x89, 0x00);
}

void gdt_initialize(void)
{
    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint32_t)&gdt;

    /* index 0: null descriptor */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* index 1: kernel code DPL 0, executable, readable */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xCF);

    /* index 2: kernel data DPL 0, writable */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xCF);

    /* index 3: user code DPL 3, executable, readable */
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xCF);

    /* index 4: user data DPL 3, writable */
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xCF);

    /* index 5: TSS */
    tss_install(5);

    gdt_flush((uint32_t)&gp);
    tss_flush();
}

void tss_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

/**
 * the x86_64 GDT and TSS. the i686 kernel uses gdt.c.
 *
 * in long mode the CPU ignores the base and limit of the code and data
 * segments. the GDT only gives the privilege levels and the 64-bit code
 * flag. the TSS has no task state; it holds RSP0 (the kernel stack for an
 * interrupt from user mode) and the IST stacks.
 */

#ifdef __x86_64__

#include "gdt.h"
#include "common/memory.h"

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
    uint64_t base;
} __attribute__((packed));

struct tss64
{
    uint32_t reserved0;
    uint64_t rsp0; // the kernel stack for an interrupt from user mode
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7]; // IST1 to IST7
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

// access bytes
#define ACCESS_KERNEL_CODE 0x9A // present, DPL 0, code, readable
#define ACCESS_KERNEL_DATA 0x92 // present, DPL 0, data, writable
#define ACCESS_USER_CODE   0xFA // present, DPL 3, code, readable
#define ACCESS_USER_DATA   0xF2 // present, DPL 3, data, writable
#define ACCESS_TSS         0x89 // present, DPL 0, available 64-bit TSS

// granularity byte: bit 5 (L) marks a 64-bit code segment
#define FLAGS_LONG_CODE 0x20
#define FLAGS_DATA      0x00

// 5 segments and the 16-byte TSS descriptor (2 entries)
static struct gdt_entry gdt[7];
static struct gdt_ptr gp;
static struct tss64 tss;

uintptr_t syscall_kernel_rsp;

static void set_entry(int index, uint8_t access, uint8_t flags)
{
    memset(&gdt[index], 0, sizeof(gdt[index]));
    gdt[index].access = access;
    gdt[index].granularity = flags;
}

/**
 * the TSS descriptor is 16 bytes: a normal descriptor with the low 32 bits
 * of the base, then the high 32 bits of the base.
 */
static void set_tss_descriptor(int index)
{
    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint32_t limit = sizeof(tss) - 1;

    gdt[index].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[index].base_low = (uint16_t)(base & 0xFFFF);
    gdt[index].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[index].access = ACCESS_TSS;
    gdt[index].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFF);

    uint32_t high = (uint32_t)(base >> 32);
    memcpy(&gdt[index + 1], &high, sizeof(high));
    memset((uint8_t *)&gdt[index + 1] + sizeof(high), 0, sizeof(gdt[0]) - sizeof(high));
}

void gdt_initialize(void)
{
    memset(&tss, 0, sizeof(tss));
    tss.iomap_base = sizeof(tss); // no I/O permission bitmap

    memset(&gdt[0], 0, sizeof(gdt[0]));
    set_entry(1, ACCESS_KERNEL_CODE, FLAGS_LONG_CODE);
    set_entry(2, ACCESS_KERNEL_DATA, FLAGS_DATA);
    set_entry(3, ACCESS_USER_DATA, FLAGS_DATA);
    set_entry(4, ACCESS_USER_CODE, FLAGS_LONG_CODE);
    set_tss_descriptor(5);

    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint64_t)(uintptr_t)&gdt;

    // load the GDT, then the data segments, then CS with a far return
    __asm__ volatile("lgdt %0\n"
                     "mov %1, %%ax\n"
                     "mov %%ax, %%ds\n"
                     "mov %%ax, %%es\n"
                     "mov %%ax, %%ss\n"
                     "xor %%eax, %%eax\n"
                     "mov %%ax, %%fs\n"
                     "mov %%ax, %%gs\n"
                     "pushq %2\n"
                     "leaq 1f(%%rip), %%rax\n"
                     "pushq %%rax\n"
                     "lretq\n"
                     "1:\n"
                     :
                     : "m"(gp), "i"(GDT_KERNEL_DATA), "i"(GDT_KERNEL_CODE)
                     : "rax", "memory");

    __asm__ volatile("ltr %w0" : : "r"((uint16_t)GDT_TSS));
}

void tss_set_kernel_stack(uintptr_t rsp0)
{
    tss.rsp0 = rsp0;
    syscall_kernel_rsp = rsp0;
}

void tss_set_ist(int index, uintptr_t stack_top)
{
    if (index >= 1 && index <= 7)
    {
        tss.ist[index - 1] = stack_top;
    }
}

#endif // __x86_64__

#include "arch/arch.h"
#include "arch/x86/cpu/io.h"
#include "arch/x86/cpu/gdt.h"

void arch_disable_interrupts(void)
{
    io_disable_interrupts();
}

void arch_enable_interrupts(void)
{
    io_enable_interrupts();
}

void arch_halt(void)
{
    __asm__ __volatile__("hlt");
}

void arch_halt_forever(void)
{
    for (;;)
    {
        __asm__ __volatile__("cli; hlt");
    }
}

uint32_t arch_get_stack_pointer(void)
{
    uint32_t esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp));
    return esp;
}

void arch_switch_address_space(uint32_t page_table_phys)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(page_table_phys) : "memory");
}

void arch_jump_to_usermode(uint32_t entry, uint32_t user_esp)
{
    __asm__ volatile(
        "cli\n"
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "\n"
        "push %0\n" /* SS */
        "push %1\n" /* ESP */
        "pushf\n"
        "pop %%eax\n"
        "or $0x200, %%eax\n" /* IF */
        "push %%eax\n"       /* EFLAGS */
        "push %2\n"          /* CS */
        "push %3\n"          /* EIP */
        "iret\n"
        :
        : "i"(GDT_USER_DATA),
          "r"(user_esp),
          "i"(GDT_USER_CODE),
          "r"(entry)
        : "eax", "memory");

    __builtin_unreachable();
}

void arch_reload_segments(void)
{
    __asm__ volatile(
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        :
        : "i"(GDT_KERNEL_DATA)
        : "eax");
}

#include "usermode.h"

#include "../../arch/x86/cpu/gdt.h"
#include "../../arch/x86/cpu/paging.h"
#include "../memory/pmm.h"
#include "../panic.h"
#include "../../common/stdio.h"
#include "../../common/memory.h"

/** size of the user stack in bytes (one page). */
#define USER_STACK_SIZE 4096

void jump_to_usermode(uint32_t entry)
{
    /* allocate a physical page for the user stack */
    void *stack_page = pmm_alloc_page();
    if (!stack_page)
    {
        kernel_panic("jump_to_usermode: failed to allocate user stack");
    }

    uint32_t stack_phys = (uint32_t)stack_page;

    /* mark the stack page as user-accessible in the page tables.
     * since we're identity-mapped, virt == phys. */
    paging_map(stack_phys, stack_phys, PTE_USER_RW);

    /* also mark the entry point page as user-accessible so the CPU
     * can fetch instructions from it in ring 3. */
    uint32_t entry_page = entry & ~0xFFFu;
    paging_map(entry_page, entry_page, PTE_USER_RW);

    /* user stack grows downward — ESP starts at the top of the page */
    uint32_t user_esp = stack_phys + USER_STACK_SIZE;

    /* set the kernel stack in the TSS so that int 0x80 from ring 3
     * switches to a valid kernel stack. we use the current ESP as
     * the kernel stack top (we're about to leave kernel mode). */
    uint32_t kernel_esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(kernel_esp));
    tss_set_kernel_stack(kernel_esp);

    printf("usermode: jumping to 0x%x (stack 0x%x)\r\n", entry, user_esp);

    /* build the iret frame on the current kernel stack and fire.
     * iret expects (from top): EIP, CS, EFLAGS, ESP, SS
     * when returning to a different privilege level. */
    __asm__ volatile(
        "cli\n"
        "mov %0, %%ax\n"       /* user data segment selector */
        "mov %%ax, %%ds\n"     /* set DS to user data */
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "\n"
        "push %0\n"            /* SS = user data selector */
        "push %1\n"            /* ESP = user stack pointer */
        "pushf\n"              /* EFLAGS (we'll OR in IF below) */
        "pop %%eax\n"
        "or $0x200, %%eax\n"   /* set IF so interrupts work in userspace */
        "push %%eax\n"         /* push modified EFLAGS */
        "push %2\n"            /* CS = user code selector */
        "push %3\n"            /* EIP = entry point */
        "iret\n"
        :
        : "i"(GDT_USER_DATA),
          "r"(user_esp),
          "i"(GDT_USER_CODE),
          "r"(entry)
        : "eax", "memory"
    );

    /* unreachable */
    kernel_panic("jump_to_usermode: iret returned");
}

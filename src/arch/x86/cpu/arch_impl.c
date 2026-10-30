#include "arch/arch.h"
#include "arch/x86/cpu/io.h"
#include "arch/x86/cpu/gdt.h"
#include "arch/x86/cpu/paging.h"

void arch_disable_interrupts(void)
{
    io_disable_interrupts();
}

void arch_enable_interrupts(void)
{
    io_enable_interrupts();
}

uint32_t arch_irq_save(void)
{
    uint32_t flags;

    // push EFLAGS, pop it into flags, then mask interrupts. the saved
    // copy still reflects the interrupt-enable bit as it was on entry.
    __asm__ volatile("pushf\n"
                     "pop %0\n"
                     "cli\n"
                     : "=r"(flags)
                     :
                     : "memory");
    return flags;
}

void arch_irq_restore(uint32_t flags)
{
    // restore the whole EFLAGS word; this puts the interrupt-enable bit
    // back to its saved value rather than unconditionally enabling it.
    __asm__ volatile("push %0\n"
                     "popf\n"
                     :
                     : "r"(flags)
                     : "memory", "cc");
}

bool arch_irqs_enabled(void)
{
    uint32_t flags;
    __asm__ volatile("pushf\n"
                     "pop %0\n"
                     : "=r"(flags));
    return (flags & 0x200u) != 0; // EFLAGS.IF
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

// a frame word can be read without faulting. before paging is on every
// physical address is readable; after, the page must be mapped.
static bool frame_word_readable(uint32_t addr)
{
    if (!paging_is_enabled())
    {
        return true;
    }
    return paging_get_physical(addr & ~0xFFFu) != 0;
}

int arch_stack_walk(uint32_t frame, uint32_t *pcs, int max)
{
    int depth = 0;

    // with frame pointers each frame is [saved ebp][return address]. the
    // entry code and new kernel threads start with ebp = 0, ending the chain.
    while (depth < max && frame != 0)
    {
        if ((frame & 3u) != 0 || !frame_word_readable(frame) ||
            !frame_word_readable(frame + 4))
        {
            break;
        }

        const uint32_t *words = (const uint32_t *)frame;
        uint32_t next = words[0];
        uint32_t ret = words[1];
        if (ret == 0)
        {
            break;
        }
        pcs[depth++] = ret;

        // the stack grows down, so a caller's frame is always higher.
        if (next <= frame)
        {
            break;
        }
        frame = next;
    }

    return depth;
}

void arch_switch_address_space(addr_space_t as)
{
    // defer to the paging layer so CR3 loading lives in exactly one place.
    paging_switch_directory(as);
}

addr_space_t arch_kernel_address_space(void)
{
    return paging_directory_address();
}

addr_space_t arch_create_address_space(void)
{
    // paging_clone_directory returns 0 on failure, which is ADDR_SPACE_NONE.
    return paging_clone_directory();
}

void arch_destroy_address_space(addr_space_t as)
{
    paging_free_directory(as);
}

// translate the neutral MMU_FLAG_* bits into x86 page-table entry flags.
static uint32_t pte_flags_from_mmu(uint32_t mmu_flags)
{
    uint32_t pte = 0;
    if (mmu_flags & MMU_FLAG_PRESENT)
    {
        pte |= PTE_PRESENT;
    }
    if (mmu_flags & MMU_FLAG_WRITE)
    {
        pte |= PTE_READ_WRITE;
    }
    if (mmu_flags & MMU_FLAG_USER)
    {
        pte |= PTE_USER;
    }
    return pte;
}

int arch_map_page(addr_space_t as, uint32_t virt, uint32_t phys, uint32_t flags)
{
    if (!paging_map_in(as, virt, phys, pte_flags_from_mmu(flags)))
    {
        return -1;
    }
    return 0;
}

uint32_t arch_translate_in(addr_space_t as, uint32_t virt)
{
    return paging_get_physical_in(as, virt);
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

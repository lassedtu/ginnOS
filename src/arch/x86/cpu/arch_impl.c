#include "arch/arch.h"
#include "arch/x86/cpu/io.h"
#include "arch/x86/cpu/gdt.h"
#include "arch/x86/cpu/paging.h"
#include "arch/x86/cpu/pat.h"

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
    uintptr_t flags;

    // push EFLAGS, pop it into flags, then mask interrupts. the saved
    // copy still reflects the interrupt-enable bit as it was on entry.
    // (the bits above 31 of RFLAGS are zero.)
    __asm__ volatile("pushf\n"
                     "pop %0\n"
                     "cli\n"
                     : "=r"(flags)
                     :
                     : "memory");
    return (uint32_t)flags;
}

void arch_irq_restore(uint32_t flags)
{
    // restore the whole EFLAGS word; this puts the interrupt-enable bit
    // back to its saved value rather than unconditionally enabling it.
    uintptr_t value = flags;
    __asm__ volatile("push %0\n"
                     "popf\n"
                     :
                     : "r"(value)
                     : "memory", "cc");
}

bool arch_irqs_enabled(void)
{
    uintptr_t flags;
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

uintptr_t arch_get_stack_pointer(void)
{
    uintptr_t sp;
#ifdef __x86_64__
    __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
#else
    __asm__ volatile("mov %%esp, %0" : "=r"(sp));
#endif
    return sp;
}

// a frame word can be read without faulting. before paging is on every
// physical address is readable; after, the page must be mapped.
static bool frame_word_readable(uintptr_t addr)
{
    if (!paging_is_enabled())
    {
        return true;
    }
    return paging_get_physical(PAGE_ALIGN_DOWN(addr)) != 0;
}

int arch_stack_walk(uintptr_t frame, uintptr_t *pcs, int max)
{
    int depth = 0;

    // with frame pointers each frame is [saved ebp][return address]. the
    // entry code and new kernel threads start with ebp = 0, ending the chain.
    while (depth < max && frame != 0)
    {
        if ((frame & (sizeof(uintptr_t) - 1)) != 0 || !frame_word_readable(frame) ||
            !frame_word_readable(frame + sizeof(uintptr_t)))
        {
            break;
        }

        const uintptr_t *words = (const uintptr_t *)frame;
        uintptr_t next = words[0];
        uintptr_t ret = words[1];
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

bool arch_copy_user_space(addr_space_t dst, addr_space_t src)
{
    return paging_copy_user(dst, src);
}

uint32_t arch_count_user_pages(addr_space_t as)
{
    return paging_count_user(as);
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
    if (mmu_flags & MMU_FLAG_SHARED)
    {
        pte |= PTE_SHARED;
    }
    return pte;
}

int arch_map_page(addr_space_t as, uintptr_t virt, phys_addr_t phys, uint32_t flags)
{
    if (!paging_map_in(as, virt, phys, pte_flags_from_mmu(flags)))
    {
        return -1;
    }
    return 0;
}

void arch_reserve_kernel_range(uintptr_t start, uintptr_t end)
{
    paging_reserve_tables(start, end);
}

void arch_map_kernel_page(uintptr_t virt, phys_addr_t phys)
{
    paging_map(virt, phys, PTE_KERNEL_RW);
}

void arch_map_device_page(uintptr_t virt, phys_addr_t phys)
{
    paging_map(virt, phys, PTE_KERNEL_RW | PTE_CACHE_DISABLE | PTE_WRITE_THROUGH);
}

void arch_map_wc_page(uintptr_t virt, phys_addr_t phys)
{
    // PWT alone selects PAT entry 1, write-combining after pat_init
    uint32_t flags = pat_has_write_combining() ? PTE_WRITE_THROUGH
                                               : (PTE_CACHE_DISABLE | PTE_WRITE_THROUGH);
    paging_map(virt, phys, PTE_KERNEL_RW | flags);
}

void arch_unmap_kernel_page(uintptr_t virt)
{
    paging_unmap(virt);
}

bool arch_user_access_ok(addr_space_t as, uintptr_t addr, size_t len, bool write)
{
    return paging_user_range_ok(as, addr, len, write);
}

phys_addr_t arch_unmap_page(addr_space_t as, uintptr_t virt)
{
    return paging_unmap_in(as, virt);
}

bool arch_protect_page(addr_space_t as, uintptr_t virt, uint32_t flags)
{
    return paging_protect_in(as, virt, pte_flags_from_mmu(flags));
}

phys_addr_t arch_translate_in(addr_space_t as, uintptr_t virt)
{
    return paging_get_physical_in(as, virt);
}

void arch_jump_to_usermode(uintptr_t entry, uintptr_t user_esp)
{
#ifdef __x86_64__
    // an iretq frame: ss, rsp, rflags (IF set), cs, rip. the general
    // registers are not cleared: the first process is started this way
    // only once, by the kernel.
    __asm__ volatile(
        "cli\n"
        "pushq %0\n" /* SS */
        "pushq %1\n" /* RSP */
        "pushfq\n"
        "orq $0x200, (%%rsp)\n" /* IF */
        "pushq %2\n"            /* CS */
        "pushq %3\n"            /* RIP */
        "iretq\n"
        :
        : "i"(GDT_USER_DATA),
          "r"(user_esp),
          "i"(GDT_USER_CODE),
          "r"(entry)
        : "memory");
#else
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
#endif

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

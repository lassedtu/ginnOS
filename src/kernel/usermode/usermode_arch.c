#include "usermode.h"

#include "arch/arch.h"
#include "arch/x86/cpu/gdt.h"
#include "arch/x86/cpu/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/memory_layout.h"
#include "kernel/panic.h"
#include "common/memory.h"
#include "common/string.h"

#include "arch/x86/cpu/isr.h"

// EFLAGS for a fresh program: interrupts on, the reserved bit 1 set
#define USER_EFLAGS 0x202u

/**
 * count the entries of a NULL-terminated string vector (NULL counts as empty).
 */
static uint32_t vector_count(const char **vector)
{
    uint32_t count = 0;
    while (vector && vector[count])
    {
        count++;
    }
    return count;
}

/**
 * copy the strings of a vector down from *sp and fill pointers[] with their
 * user addresses.
 */
static void push_strings(uintptr_t *sp, const char **vector, uint32_t count, uintptr_t *pointers)
{
    for (uint32_t i = count; i-- > 0;)
    {
        size_t len = strlen(vector[i]) + 1; // include the NUL
        *sp -= len;
        memcpy((void *)*sp, vector[i], len);
        pointers[i] = *sp;
    }
}

/**
 * push a NULL-terminated pointer array; return its user address.
 */
static uintptr_t push_pointers(uintptr_t *sp, const uintptr_t *pointers, uint32_t count)
{
    *sp -= sizeof(uintptr_t);
    *(uintptr_t *)*sp = 0;
    for (uint32_t i = count; i-- > 0;)
    {
        *sp -= sizeof(uintptr_t);
        *(uintptr_t *)*sp = pointers[i];
    }
    return *sp;
}

/**
 * push one word.
 */
static void push_word(uintptr_t *sp, uintptr_t value)
{
    *sp -= sizeof(uintptr_t);
    *(uintptr_t *)*sp = value;
}

uintptr_t usermode_build_stack(addr_space_t as, const char **argv, const char **envp)
{
    // back the top pages of the stack area now; the rest are backed on
    // demand. the page above USER_STACK_TOP stays an unmapped guard.
    for (uintptr_t page = USER_STACK_BASE; page < USER_STACK_TOP; page += PAGE_SIZE)
    {
        void *frame = pmm_alloc_page();
        if (!frame)
        {
            return 0;
        }
        memset(frame, 0, PAGE_SIZE);
        if (!paging_map_in(as, page, virt_to_phys(frame), PTE_USER_RW))
        {
            pmm_free_page(frame);
            return 0;
        }
    }

    uint32_t argc = vector_count(argv);
    uint32_t envc = vector_count(envp);
    if (argc > USER_ARGC_MAX)
    {
        argc = USER_ARGC_MAX;
    }
    if (envc > USER_ENVC_MAX)
    {
        envc = USER_ENVC_MAX;
    }

    // the strings at the top, then the two pointer arrays, then the words
    // crt0 reads: [esp] fake return address, argc, argv, envp
    uintptr_t argv_strings[USER_ARGC_MAX];
    uintptr_t envp_strings[USER_ENVC_MAX];
    uintptr_t sp = USER_STACK_TOP;
    push_strings(&sp, envp, envc, envp_strings);
    push_strings(&sp, argv, argc, argv_strings);
    sp &= ~(uintptr_t)(sizeof(uintptr_t) - 1);

    uintptr_t envp_user = push_pointers(&sp, envp_strings, envc);
    uintptr_t argv_user = push_pointers(&sp, argv_strings, argc);
    push_word(&sp, envp_user);
    push_word(&sp, argv_user);
    push_word(&sp, argc);
    push_word(&sp, 0); // fake return address: _start never returns
    return sp;
}

void jump_to_usermode(uintptr_t entry, addr_space_t as, const char **argv, const char **envp)
{
    uintptr_t user_esp = usermode_build_stack(as, argv, envp);
    if (!user_esp)
    {
        kernel_panic("jump_to_usermode: no memory for the user stack");
    }

    // set the kernel stack in the TSS to the current kernel ESP
    uintptr_t kernel_esp = arch_get_stack_pointer();
    tss_set_kernel_stack(kernel_esp);

    // drop to ring 3
    arch_jump_to_usermode(entry, user_esp);

    kernel_panic("jump_to_usermode: iret returned");
}

void usermode_reset_frame(struct registers *regs, uintptr_t entry, uintptr_t user_esp)
{
    regs->edi = 0;
    regs->esi = 0;
    regs->ebp = 0;
    regs->ebx = 0;
    regs->edx = 0;
    regs->ecx = 0;
    regs->eax = 0;
    regs->eip = (uint32_t)entry;
    regs->esp = (uint32_t)user_esp;
    regs->eflags = USER_EFLAGS;
    regs->cs = GDT_USER_CODE;
    regs->ss = GDT_USER_DATA;
    regs->ds = GDT_USER_DATA;
}

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

/** the bytes of a vector's strings, their NULs included */
static size_t vector_bytes(const char **vector, uint32_t count)
{
    size_t bytes = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        bytes += strlen(vector[i]) + 1;
    }
    return bytes;
}

/**
 * copy the strings of a vector to *at, upwards, and write their user
 * addresses as a NULL-terminated pointer array at pointers.
 */
static void put_vector(uintptr_t *at, const char **vector, uint32_t count, uintptr_t *pointers)
{
    for (uint32_t i = 0; i < count; i++)
    {
        size_t len = strlen(vector[i]) + 1; // include the NUL
        memcpy((void *)*at, vector[i], len);
        pointers[i] = *at;
        *at += len;
    }
    pointers[count] = 0;
}

uintptr_t usermode_build_stack(addr_space_t as, const char **argv, const char **envp)
{
    uint32_t argc = vector_count(argv);
    uint32_t envc = vector_count(envp);

    // the layout, from the top down: the strings; the two pointer arrays;
    // [esp] fake return address, argc, argv, envp. the pointer part starts
    // 16-byte aligned.
    size_t strings = vector_bytes(argv, argc) + vector_bytes(envp, envc);
    size_t words = 4 + (argc + 1) + (envc + 1);
    uintptr_t string_start = (USER_STACK_TOP - strings) & ~(uintptr_t)(sizeof(uintptr_t) - 1);
    uintptr_t sp = (string_start - words * sizeof(uintptr_t)) & ~(uintptr_t)15;

    // back the pages the arguments take and USER_STACK_SIZE more under
    // them; the rest are backed on demand. the page above USER_STACK_TOP
    // stays an unmapped guard.
    uintptr_t lowest = (sp & ~(uintptr_t)(PAGE_SIZE - 1)) - USER_STACK_SIZE;
    if (lowest > USER_STACK_BASE)
    {
        lowest = USER_STACK_BASE;
    }
    for (uintptr_t page = lowest; page < USER_STACK_TOP; page += PAGE_SIZE)
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

    uintptr_t *words_at = (uintptr_t *)sp;
    uintptr_t *argv_user = words_at + 4;
    uintptr_t *envp_user = argv_user + argc + 1;
    uintptr_t at = string_start;
    put_vector(&at, argv, argc, argv_user);
    put_vector(&at, envp, envc, envp_user);
    words_at[0] = 0; // fake return address: _start never returns
    words_at[1] = argc;
    words_at[2] = (uintptr_t)argv_user;
    words_at[3] = (uintptr_t)envp_user;
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
    // all general registers start at zero
    struct registers clean;
    memset(&clean, 0, sizeof(clean));
    clean.interrupt = regs->interrupt;
    clean.error = regs->error;
    *regs = clean;

    REG_IP(regs) = entry;
    REG_SP(regs) = user_esp;
    REG_FLAGS(regs) = USER_EFLAGS;
    regs->cs = GDT_USER_CODE;
    regs->ss = GDT_USER_DATA;
#ifndef __x86_64__
    regs->ds = GDT_USER_DATA;
#endif
}

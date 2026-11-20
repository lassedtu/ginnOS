#include "usermode.h"

#include "arch/arch.h"
#include "arch/x86/cpu/gdt.h"
#include "arch/x86/cpu/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/memory_layout.h"
#include "kernel/panic.h"
#include "common/memory.h"
#include "common/string.h"

// size of the user stack in bytes (4 pages = 16 KiB).
#define USER_STACK_SIZE (4 * PAGE_SIZE)

// maximum number of command-line arguments passed to a user program.
#define ARGV_MAX 64

void jump_to_usermode(uintptr_t entry, addr_space_t as, const char **argv)
{
    // allocate physical pages for the user stack and map them at a fixed
    // virtual address range just below the program load address.
    // stack region: USER_STACK_BASE - USER_LOAD_ADDR (16 KiB, 4 pages)
    uint32_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    uintptr_t stack_virt_base = USER_LOAD_ADDR - USER_STACK_SIZE;

    for (uint32_t p = 0; p < stack_pages; p++)
    {
        void *frame = pmm_alloc_page();
        if (!frame)
        {
            kernel_panic("jump_to_usermode: failed to allocate user stack page");
        }
        memset(frame, 0, PAGE_SIZE);
        paging_map_in(as, stack_virt_base + p * PAGE_SIZE, virt_to_phys(frame), PTE_USER_RW);
    }

    // user stack grows downward. start at the top of the stack region
    uintptr_t stack_top = stack_virt_base + USER_STACK_SIZE; // 0x800000
    uintptr_t sp = stack_top;

    // count arguments
    int argc = 0;
    if (argv)
    {
        while (argv[argc])
            argc++;
    }

    // copy argument strings onto the top of the user stack
    // string_ptrs[i] will hold the user-space pointer to each string
    uintptr_t string_ptrs[ARGV_MAX];
    if (argc > ARGV_MAX)
        argc = ARGV_MAX;

    for (int i = argc - 1; i >= 0; i--)
    {
        size_t len = strlen(argv[i]) + 1; // include null terminator
        sp -= len;
        memcpy((void *)sp, argv[i], len);
        string_ptrs[i] = sp;
    }

    // align the stack to a word after the string copies
    sp &= ~(uintptr_t)(sizeof(uintptr_t) - 1);

    // push NULL terminator for argv array
    sp -= sizeof(uintptr_t);
    *(uintptr_t *)sp = 0;

    // push argv[argc-1] ... argv[0] (pointers to the strings)
    for (int i = argc - 1; i >= 0; i--)
    {
        sp -= sizeof(uintptr_t);
        *(uintptr_t *)sp = string_ptrs[i];
    }

    // sp now points to argv[0], this is the value of argv
    uintptr_t argv_ptr = sp;

    // push argv pointer
    sp -= sizeof(uintptr_t);
    *(uintptr_t *)sp = argv_ptr;

    // push argc
    sp -= sizeof(uintptr_t);
    *(uintptr_t *)sp = (uintptr_t)argc;

    // push a fake return address (user programs shouldn't return from _start,
    // but this keeps the stack aligned and matches calling convention)
    sp -= sizeof(uintptr_t);
    *(uintptr_t *)sp = 0;

    uintptr_t user_esp = sp;

    // set the kernel stack in the TSS to the current kernel ESP
    uintptr_t kernel_esp = arch_get_stack_pointer();
    tss_set_kernel_stack(kernel_esp);

    // drop to ring 3
    arch_jump_to_usermode(entry, user_esp);

    kernel_panic("jump_to_usermode: iret returned");
}

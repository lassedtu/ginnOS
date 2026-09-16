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
#define USER_STACK_SIZE (4096 * 4)

// maximum number of command-line arguments passed to a user program.
#define ARGV_MAX 64

void jump_to_usermode(uint32_t entry, uint32_t pd_phys, const char **argv)
{
    // allocate physical pages for the user stack and map them at a fixed
    // virtual address range just below the program load address.
    // stack region: USER_STACK_BASE - USER_LOAD_ADDR (16 KiB, 4 pages)
    uint32_t stack_pages = USER_STACK_SIZE / 4096;
    uint32_t stack_virt_base = USER_LOAD_ADDR - USER_STACK_SIZE;

    for (uint32_t p = 0; p < stack_pages; p++)
    {
        void *frame = pmm_alloc_page();
        if (!frame)
        {
            kernel_panic("jump_to_usermode: failed to allocate user stack page");
        }
        memset(frame, 0, 4096);
        paging_map_in(pd_phys, stack_virt_base + p * 4096, (uint32_t)frame, PTE_USER_RW);
    }

    // user stack grows downward. start at the top of the stack region
    uint32_t stack_top = stack_virt_base + USER_STACK_SIZE; // 0x800000
    uint32_t sp = stack_top;

    // count arguments
    int argc = 0;
    if (argv)
    {
        while (argv[argc])
            argc++;
    }

    // copy argument strings onto the top of the user stack
    // string_ptrs[i] will hold the user-space pointer to each string
    uint32_t string_ptrs[ARGV_MAX];
    if (argc > ARGV_MAX)
        argc = ARGV_MAX;

    for (int i = argc - 1; i >= 0; i--)
    {
        uint32_t len = strlen(argv[i]) + 1; // include null terminator
        sp -= len;
        memcpy((void *)sp, argv[i], len);
        string_ptrs[i] = sp;
    }

    // align stack to 4 bytes after string copies
    sp &= ~3u;

    // push NULL terminator for argv array
    sp -= 4;
    *(uint32_t *)sp = 0;

    // push argv[argc-1] ... argv[0] (pointers to the strings)
    for (int i = argc - 1; i >= 0; i--)
    {
        sp -= 4;
        *(uint32_t *)sp = string_ptrs[i];
    }

    // sp now points to argv[0], this is the value of argv
    uint32_t argv_ptr = sp;

    // push argv pointer
    sp -= 4;
    *(uint32_t *)sp = argv_ptr;

    // push argc
    sp -= 4;
    *(uint32_t *)sp = (uint32_t)argc;

    // push a fake return address (user programs shouldn't return from _start,
    // but this keeps the stack aligned and matches calling convention)
    sp -= 4;
    *(uint32_t *)sp = 0;

    uint32_t user_esp = sp;

    // set the kernel stack in the TSS to the current kernel ESP
    uint32_t kernel_esp = arch_get_stack_pointer();
    tss_set_kernel_stack(kernel_esp);

    // drop to ring 3
    arch_jump_to_usermode(entry, user_esp);

    kernel_panic("jump_to_usermode: iret returned");
}

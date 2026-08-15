#include "usermode.h"

#include "../../arch/x86/cpu/gdt.h"
#include "../../arch/x86/cpu/paging.h"
#include "../memory/pmm.h"
#include "../elf/elf_loader.h"
#include "../panic.h"
#include "../../common/stdio.h"
#include "../../common/memory.h"

/** size of the user stack in bytes (one page). */
#define USER_STACK_SIZE 4096

/** jump buffer: 6 x uint32_t (ebx, esi, edi, ebp, esp, eip). */
typedef uint32_t kernel_jmp_buf[6];

extern int kernel_setjmp(kernel_jmp_buf buf);
extern void kernel_longjmp(kernel_jmp_buf buf, int val) __attribute__((noreturn));

/** saved context for returning from userspace to exec_program's caller. */
static kernel_jmp_buf exec_jmp_buf;

void jump_to_usermode(uint32_t entry)
{
    /* allocate a physical page for the user stack */
    void *stack_page = pmm_alloc_page();
    if (!stack_page)
    {
        kernel_panic("jump_to_usermode: failed to allocate user stack");
    }

    uint32_t stack_phys = (uint32_t)stack_page;

    /* mark the stack page as user-accessible */
    paging_map(stack_phys, stack_phys, PTE_USER_RW);

    /* user stack grows downward — ESP starts at the top of the page */
    uint32_t user_esp = stack_phys + USER_STACK_SIZE;

    /* set the kernel stack in the TSS */
    uint32_t kernel_esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(kernel_esp));
    tss_set_kernel_stack(kernel_esp);

    /* build the iret frame and drop to ring 3 */
    __asm__ volatile(
        "cli\n"
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "\n"
        "push %0\n"            /* SS */
        "push %1\n"            /* ESP */
        "pushf\n"
        "pop %%eax\n"
        "or $0x200, %%eax\n"   /* IF */
        "push %%eax\n"         /* EFLAGS */
        "push %2\n"            /* CS */
        "push %3\n"            /* EIP */
        "iret\n"
        :
        : "i"(GDT_USER_DATA),
          "r"(user_esp),
          "i"(GDT_USER_CODE),
          "r"(entry)
        : "eax", "memory"
    );

    kernel_panic("jump_to_usermode: iret returned");
}

int exec_program(const char *path)
{
    elf_load_result_t elf;

    if (!elf_load(path, &elf))
    {
        return -1;
    }

    /* save kernel context. when usermode_exit calls longjmp,
     * execution resumes here with the exit code as return value. */
    int code = kernel_setjmp(exec_jmp_buf);
    if (code != 0)
    {
        /* returned from usermode_exit — restore kernel segments */
        __asm__ volatile(
            "mov %0, %%ax\n"
            "mov %%ax, %%ds\n"
            "mov %%ax, %%es\n"
            "mov %%ax, %%fs\n"
            "mov %%ax, %%gs\n"
            :
            : "i"(GDT_KERNEL_DATA)
            : "eax"
        );
        /* code is exit_code + 1 (longjmp forces nonzero), so subtract 1.
         * but we pass exit_code + 1 in usermode_exit to handle code==0. */
        return code - 1;
    }

    jump_to_usermode(elf.entry);

    return -1; /* unreachable */
}

void usermode_exit(int exit_code)
{
    /* longjmp back to exec_program's setjmp point.
     * pass exit_code + 1 since longjmp forces nonzero (0 becomes 1). */
    kernel_longjmp(exec_jmp_buf, exit_code + 1);
}

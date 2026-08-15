#include "usermode.h"

#include "../../arch/x86/cpu/gdt.h"
#include "../../arch/x86/cpu/paging.h"
#include "../memory/pmm.h"
#include "../elf/elf_loader.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"
#include "../panic.h"
#include "../../common/stdio.h"
#include "../../common/memory.h"

// size of the user stack in bytes (one page).
#define USER_STACK_SIZE 4096

// jump buffer: 6 x uint32_t (ebx, esi, edi, ebp, esp, eip).
typedef uint32_t kernel_jmp_buf[6];

extern int kernel_setjmp(kernel_jmp_buf buf);
extern void kernel_longjmp(kernel_jmp_buf buf, int val) __attribute__((noreturn));
extern void context_switch(uint32_t *old_esp, uint32_t new_esp);

// saved context for returning from userspace to exec_program's caller.
// used ONLY for kernel-shell synchronous exec (no current process).
static kernel_jmp_buf exec_jmp_buf;

uint32_t usermode_get_brk(void)
{
    process_t *proc = process_current();
    if (!proc)
    {
        return 0;
    }
    return proc->brk;
}

void usermode_set_brk(uint32_t brk)
{
    process_t *proc = process_current();
    if (proc)
    {
        proc->brk = brk;
    }
}

void jump_to_usermode(uint32_t entry)
{
    // allocate a physical page for the user stack
    void *stack_page = pmm_alloc_page();
    if (!stack_page)
    {
        kernel_panic("jump_to_usermode: failed to allocate user stack");
    }

    uint32_t stack_phys = (uint32_t)stack_page;

    // map the stack page as user-accessible in the current process's page directory
    process_t *proc = process_current();
    uint32_t pd = proc ? proc->page_directory : paging_directory_address();
    paging_map_in(pd, stack_phys, stack_phys, PTE_USER_RW);

    // user stack grows downward ESP starts at the top of the page
    uint32_t user_esp = stack_phys + USER_STACK_SIZE;

    // set the kernel stack in the TSS to the current kernel ESP
    uint32_t kernel_esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(kernel_esp));
    tss_set_kernel_stack(kernel_esp);

    // build the iret frame and drop to ring 3
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

    kernel_panic("jump_to_usermode: iret returned");
}

/**
 * trampoline function for child processes launched via the scheduler.
 * the child's kernel stack is set up so that context_switch returns here.
 * reads the entry point from process_current()->entry.
 * this function never returns.
 */
static void process_entry_trampoline(void)
{
    process_t *proc = process_current();
    if (!proc)
    {
        kernel_panic("trampoline: no current process");
    }

    // switch to this process's page directory
    paging_switch_directory(proc->page_directory);

    uint32_t entry = proc->entry;
    jump_to_usermode(entry);
}

/**
 * set up a child process's kernel stack so that when context_switch
 * switches to it, it "returns" into process_entry_trampoline.
 *
 * stack layout (growing downward):
 *   &trampoline        <- context_switch ret lands here
 *   0 (ebp)            <- context_switch pops these
 *   0 (ebx)
 *   0 (esi)
 *   0 (edi)            <- child's kernel_esp points here
 */
static void setup_child_stack(process_t *child, uint32_t entry)
{
    uint32_t *sp = (uint32_t *)(child->kernel_stack + KERNEL_STACK_SIZE);

    // return address for context_switch's ret instruction
    *(--sp) = (uint32_t)process_entry_trampoline;

    // fake callee-saved registers (context_switch pops: edi, esi, ebx, ebp)
    *(--sp) = 0; // ebp
    *(--sp) = 0; // ebx
    *(--sp) = 0; // esi
    *(--sp) = 0; // edi

    child->kernel_esp = (uint32_t)sp;
    child->entry = entry;
}

int exec_program(const char *path)
{
    elf_load_result_t elf;
    process_t *parent = process_current();

    // create a child process first (to get its page directory)
    process_t *child = process_create();
    if (!child)
    {
        printf("exec: process table full\r\n");
        return -1;
    }

    // load ELF into the child's page directory
    if (!elf_load(path, child->page_directory, &elf))
    {
        process_destroy(child);
        return -1;
    }

    child->brk = elf.brk;
    child->parent_pid = parent ? parent->pid : PID_NONE;

    if (parent)
    {
        // called from userspace: set up child for scheduler dispatch
        setup_child_stack(child, elf.entry);
        scheduler_ready(child);

        // return child PID to parent (for waitpid)
        return (int)child->pid;
    }
    else
    {
        // called from kernel shell: synchronous execution
        child->state = PROC_STATE_RUNNING;
        process_set_current(child);

        // switch to the child's page directory
        paging_switch_directory(child->page_directory);

        int code = kernel_setjmp(exec_jmp_buf);
        if (code != 0)
        {
            // returned from usermode_exit — restore kernel segments
            __asm__ volatile(
                "mov %0, %%ax\n"
                "mov %%ax, %%ds\n"
                "mov %%ax, %%es\n"
                "mov %%ax, %%fs\n"
                "mov %%ax, %%gs\n"
                :
                : "i"(GDT_KERNEL_DATA)
                : "eax");

            // switch back to kernel page directory
            paging_switch_directory(paging_directory_address());

            int exit_code = code - 1;
            process_destroy(child);
            process_set_current((void *)0);

            return exit_code;
        }

        jump_to_usermode(elf.entry);

        return -1; // unreachable
    }
}

void usermode_exit(int exit_code)
{
    process_t *proc = process_current();

    if (proc && proc->parent_pid != PID_NONE)
    {
        // child of another process: transition to ZOMBIE, wake parent
        proc->state = PROC_STATE_ZOMBIE;
        proc->exit_code = exit_code;

        // remove from scheduler (it's no longer runnable)
        scheduler_remove(proc);

        // check if parent is blocked waiting for us
        process_t *parent = process_get(proc->parent_pid);
        if (parent && parent->state == PROC_STATE_BLOCKED &&
            parent->wait_for_pid == proc->pid)
        {
            // wake the parent
            parent->state = PROC_STATE_READY;
            parent->wait_for_pid = PID_NONE;
            scheduler_ready(parent);
        }

        // yield this process is now a zombie and will never run again
        scheduler_yield();

        // unreachable
        kernel_panic("usermode_exit: a zombie woke up... run.");
    }
    else
    {
        // kernel-shell synchronous path
        kernel_longjmp(exec_jmp_buf, exit_code + 1);
    }
}

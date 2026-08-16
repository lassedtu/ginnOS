#include "usermode.h"

#include "../../arch/x86/cpu/gdt.h"
#include "../../arch/x86/cpu/paging.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"
#include "../elf/elf_loader.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"
#include "../panic.h"
#include "../../common/stdio.h"
#include "../../common/memory.h"
#include "../../common/string.h"

// size of the user stack in bytes (4 pages = 16 KiB).
#define USER_STACK_SIZE (4096 * 4)

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

void jump_to_usermode(uint32_t entry, uint32_t pd_phys, const char **argv)
{
    // allocate physical pages for the user stack and map them at a fixed
    // virtual address range just below the program load address (0x800000).
    // stack region: 0x7FC000 – 0x800000 (16 KiB, 4 pages)
    uint32_t stack_pages = USER_STACK_SIZE / 4096;
    uint32_t stack_virt_base = 0x800000 - USER_STACK_SIZE; // 0x7FC000

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

    // user stack grows downward — start at the top of the stack region
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
    uint32_t string_ptrs[64]; // max 64 args
    if (argc > 64)
        argc = 64;

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

    // sp now points to argv[0] — this is the value of argv
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
    const char **argv = (const char **)proc->argv;

    jump_to_usermode(entry, proc->page_directory, argv);
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

/**
 * deep-copy a null-terminated argv array into kernel heap.
 * returns a heap-allocated array of heap-allocated strings,
 * null-terminated. caller must free with argv_free().
 */
static char **argv_copy(const char **argv)
{
    if (!argv)
        return (char **)0;

    int argc = 0;
    while (argv[argc])
        argc++;

    // allocate pointer array (argc + 1 for NULL terminator)
    char **copy = (char **)kmalloc((argc + 1) * sizeof(char *));
    if (!copy)
        return (char **)0;

    for (int i = 0; i < argc; i++)
    {
        uint32_t len = strlen(argv[i]) + 1;
        copy[i] = (char *)kmalloc(len);
        if (!copy[i])
        {
            // cleanup on failure
            for (int j = 0; j < i; j++)
                kfree(copy[j]);
            kfree(copy);
            return (char **)0;
        }
        memcpy(copy[i], argv[i], len);
    }
    copy[argc] = (char *)0;
    return copy;
}

/**
 * free a heap-allocated argv array created by argv_copy().
 */
static void argv_free(char **argv)
{
    if (!argv)
        return;

    for (int i = 0; argv[i]; i++)
        kfree(argv[i]);
    kfree(argv);
}

int exec_program(const char *path, const char **argv)
{
    elf_load_result_t elf;
    process_t *parent = process_current();

    // create a child process first (to get its page directory)
    process_t *child = process_create();
    if (!child)
    {
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

    // deep-copy argv into kernel heap (survives until trampoline consumes it)
    child->argv = argv_copy(argv);

    // inherit parent's working directory and file descriptor table
    if (parent)
    {
        strcpy(child->cwd, parent->cwd);

        // copy parent's fd table to child (fd inheritance)
        for (int i = 0; i < FD_MAX; i++)
        {
            child->fds[i] = parent->fds[i];
            // increment pipe ref counts for inherited pipe fds
            if (child->fds[i].type == FD_TYPE_PIPE)
            {
                child->fds[i].pipe.buf->ref_count++;
            }
        }
    }

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
            // returned from usermode_exit  restore kernel segments
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
            argv_free(child->argv);
            child->argv = (char **)0;
            process_destroy(child);
            process_set_current((void *)0);

            return exit_code;
        }

        jump_to_usermode(elf.entry, child->page_directory, (const char **)child->argv);

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

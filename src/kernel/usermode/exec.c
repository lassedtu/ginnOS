#include "usermode.h"

#include "arch/arch.h"
#include "arch/x86/cpu/gdt.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/memory_layout.h"
#include "kernel/memory/mman.h"
#include "kernel/elf/elf_loader.h"
#include "kernel/process/process.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/panic.h"
#include "kernel/klog/klog.h"
#include "common/stdio.h"
#include "common/memory.h"
#include "common/string.h"

// exit status of a process whose execve failed after its old image was gone
#define EXEC_FAILED_EXIT_STATUS 127

/**
 * deep-copy a null-terminated argv array into kernel heap.
 * returns a heap-allocated array of heap-allocated strings,
 * null-terminated. caller must free with argv_free().
 */
static char **argv_copy(const char **argv)
{
    if (!argv)
        return NULL;

    int argc = 0;
    while (argv[argc])
        argc++;

    // allocate pointer array (argc + 1 for NULL terminator)
    char **copy = (char **)kmalloc((argc + 1) * sizeof(char *));
    if (!copy)
        return NULL;

    for (int i = 0; i < argc; i++)
    {
        size_t len = strlen(argv[i]) + 1;
        copy[i] = (char *)kmalloc(len);
        if (!copy[i])
        {
            // cleanup on failure
            for (int j = 0; j < i; j++)
                kfree(copy[j]);
            kfree(copy);
            return NULL;
        }
        memcpy(copy[i], argv[i], len);
    }
    copy[argc] = NULL;
    return copy;
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

    // load ELF into the child's page directory, recording its segments
    if (!elf_load(path, child->page_directory, &child->vmas, &elf))
    {
        process_destroy(child);
        return -1;
    }

    // the user stack area: its top pages are mapped when the process first
    // runs, the rest on demand as the stack grows down
    if (kerr_failed(vma_insert(&child->vmas, USER_STACK_LIMIT, USER_STACK_TOP,
                               PROT_READ | PROT_WRITE, VMA_STACK)))
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
            if (i < FD_STDIO_COUNT)
            {
                // always inherit stdin/stdout/stderr
                fd_copy_entry(&child->fds[i], &parent->fds[i]);
            }
            else if (parent->fds[i].type == FD_TYPE_PIPE)
            {
                // close-on-exec: pipe fds beyond stdio are NOT inherited
                // (prevents children from holding extra pipe refs)
                child->fds[i].type = FD_TYPE_NONE;
            }
            else
            {
                // inherit regular file fds
                child->fds[i] = parent->fds[i];
            }
        }
    }

    // every process, PID 1 included, starts through the scheduler: its
    // first switch lands in the entry trampoline, which drops to user mode.
    setup_child_stack(child, elf.entry);
    scheduler_ready(child);

    // return child PID to parent (for waitpid)
    return (int)child->pid;
}

kerr_t exec_replace(struct registers *regs, const char *path, const char **argv,
                    const char **envp)
{
    process_t *proc = process_current();

    // load the new program into a new address space. the old one stays
    // untouched until the load has worked, so a failed execve returns.
    vma_table_t *vmas = kmalloc(sizeof(vma_table_t));
    if (!vmas)
    {
        return KERR_NOMEM;
    }
    vma_table_init(vmas);

    addr_space_t as = arch_create_address_space();
    if (as == ADDR_SPACE_NONE)
    {
        kfree(vmas);
        return KERR_NOMEM;
    }

    elf_load_result_t elf;
    if (!elf_load(path, as, vmas, &elf) ||
        kerr_failed(vma_insert(vmas, USER_STACK_LIMIT, USER_STACK_TOP, PROT_READ | PROT_WRITE,
                               VMA_STACK)))
    {
        arch_destroy_address_space(as);
        kfree(vmas);
        return KERR_NOENT;
    }

    // no way back from here: swap in the new image. masked, so the process
    // is never scheduled with a half-changed address space.
    uint32_t flags = arch_irq_save();
    addr_space_t old = proc->page_directory;
    proc->page_directory = as;
    proc->vmas = *vmas;
    proc->brk = elf.brk;
    arch_switch_address_space(as);
    arch_irq_restore(flags);

    arch_destroy_address_space(old);
    kfree(vmas);

    uintptr_t user_esp = usermode_build_stack(as, argv, envp);
    if (!user_esp)
    {
        // the old program is gone and the new one has no stack
        arch_enable_interrupts();
        usermode_exit(EXEC_FAILED_EXIT_STATUS);
    }

    usermode_reset_frame(regs, elf.entry, user_esp);
    return KERR_OK;
}

void usermode_exit(int exit_code)
{
    process_t *proc = process_current();

    if (proc->pid == PID_INIT)
    {
        // PID 1 (init, for now /bin/sh) has no parent to reap it. with no
        // init left the system cannot do anything more: halt.
        arch_disable_interrupts();
        KLOG_WARN("kernel: init exited with code %d, system halted", exit_code);
        printf("init exited with code %d. System halted.\r\n", exit_code);
        arch_halt_forever();
    }

    // release everything but the kernel stack and slot now, while this
    // process can still sleep (closing a file may wait for the fs mutex).
    // closing the fds here also gives pipe readers EOF as soon as the
    // writer exits, not when the parent gets round to reaping it.
    process_release_resources(proc);

    // children outlive us: zombie ones are destroyed now, running ones are
    // reaped by the kernel when they exit
    process_orphan_children(proc);

    // masked from here on: becoming a zombie, waking the parent and
    // switching away must not be split by a timer tick. this context never
    // resumes, so the state is never restored.
    arch_disable_interrupts();

    proc->state = PROC_STATE_ZOMBIE;
    proc->exit_code = exit_code;
    scheduler_remove(proc);

    // parent_pid is PID_NONE if our parent exited first
    process_t *parent = process_get(proc->parent_pid);
    if (parent)
    {
        // wake the parent if it is blocked in waitpid for us
        if (parent->state == PROC_STATE_BLOCKED && parent->wait_for_pid == proc->pid)
        {
            parent->state = PROC_STATE_READY;
            parent->wait_for_pid = PID_NONE;
            scheduler_ready(parent);
        }
    }
    else
    {
        // nobody will waitpid for us: the scheduler reaps us once we're gone
        process_note_orphan_zombie();
    }

    scheduler_yield();

    // unreachable
    kernel_panic("usermode_exit: a zombie woke up... run.");
}

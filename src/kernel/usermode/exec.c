#include "usermode.h"

#include "arch/arch.h"
#include "arch/x86/cpu/gdt.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/heap.h"
#include "kernel/elf/elf_loader.h"
#include "kernel/process/process.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/panic.h"
#include "kernel/klog/klog.h"
#include "common/stdio.h"
#include "common/memory.h"
#include "common/string.h"

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
        uint32_t len = strlen(argv[i]) + 1;
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

void usermode_exit(int exit_code)
{
    process_t *proc = process_current();

    if (proc && proc->parent_pid != PID_NONE)
    {
        // masked from here on: becoming a zombie, waking the parent and
        // switching away must not be split by a timer tick. this context
        // never resumes, so the state is never restored.
        arch_disable_interrupts();

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
        // PID 1 (init, for now /bin/sh) has no parent to reap it. with no
        // init left the system cannot do anything more: halt.
        arch_disable_interrupts();
        KLOG_WARN("kernel: init exited with code %d, system halted", exit_code);
        printf("init exited with code %d. System halted.\r\n", exit_code);
        arch_halt_forever();
    }
}

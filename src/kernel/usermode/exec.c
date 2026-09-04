#include "usermode.h"

#include "arch/arch.h"
#include "arch/x86/cpu/gdt.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/heap.h"
#include "kernel/elf/elf_loader.h"
#include "kernel/process/process.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/panic.h"
#include "common/stdio.h"
#include "common/memory.h"
#include "common/string.h"

// jump buffer: 6 x uint32_t (ebx, esi, edi, ebp, esp, eip).
typedef uint32_t kernel_jmp_buf[6];

extern int kernel_setjmp(kernel_jmp_buf buf);
extern void kernel_longjmp(kernel_jmp_buf buf, int val) __attribute__((noreturn));
extern void context_switch(uint32_t *old_esp, uint32_t new_esp);

// saved context for returning from userspace to exec_program's caller.
// used ONLY for kernel-shell synchronous exec (no current process).
static kernel_jmp_buf exec_jmp_buf;

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
            if (i < FD_STDIO_COUNT)
            {
                // always inherit stdin/stdout/stderr
                child->fds[i] = parent->fds[i];
                if (child->fds[i].type == FD_TYPE_PIPE)
                {
                    child->fds[i].pipe.buf->ref_count++;
                    if (child->fds[i].pipe.dir == PIPE_READ)
                        child->fds[i].pipe.buf->read_refs++;
                    else
                        child->fds[i].pipe.buf->write_refs++;
                }
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

        // switch to the child's address space
        arch_switch_address_space(child->page_directory);

        int code = kernel_setjmp(exec_jmp_buf);
        if (code != 0)
        {
            // returned from usermode_exit, restore kernel segments
            arch_reload_segments();

            // switch back to the kernel address space
            arch_switch_address_space(arch_kernel_address_space());

            int exit_code = code - 1;
            argv_free(child->argv);
            child->argv = NULL;
            process_destroy(child);
            process_set_current(NULL);

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

#include "usermode.h"

#include "arch/arch.h"
#include "arch/x86/cpu/gdt.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/memory_layout.h"
#include "kernel/memory/mman.h"
#include "kernel/elf/elf_loader.h"
#include "kernel/process/process.h"
#include "kernel/process/wait_status.h"
#include "kernel/usermode/uaccess.h"
#include "kernel/signal/signal.h"
#include "kernel/vfs/permission.h"
#include "kernel/vfs/stat.h"
#include "kernel/vfs/vfs.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/panic.h"
#include "kernel/klog/klog.h"
#include "common/stdio.h"
#include "common/memory.h"
#include "common/string.h"

// exit status of a process whose execve failed after its old image was gone
#define EXEC_FAILED_EXIT_STATUS 127

/**
 * check that the current process may run a program file, and read its
 * mode and owner for the set-uid and set-gid bits.
 * @return KERR_OK, KERR_NOENT, or KERR_PERM (no execute right, or not a
 *         regular file).
 */
static kerr_t check_program(const char *path, vfs_stat_t *st)
{
    if (kerr_failed(vfs_stat(path, st)))
    {
        return KERR_NOENT;
    }
    process_t *proc = process_current();
    if ((st->mode & S_IFMT) != S_IFREG ||
        (proc && permission_check(st, proc->euid, proc->egid, MAY_EXEC) < 0))
    {
        return KERR_PERM;
    }
    return KERR_OK;
}

/**
 * a program with the set-uid or set-gid bit runs as the file's owner or
 * group.
 */
static void apply_set_ids(process_t *proc, const vfs_stat_t *st)
{
    if (st->mode & S_ISUID)
    {
        proc->euid = st->uid;
        proc->suid = st->uid;
    }
    if (st->mode & S_ISGID)
    {
        proc->egid = st->gid;
        proc->sgid = st->gid;
    }
}

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

    vfs_stat_t program;
    if (kerr_failed(check_program(path, &program)))
    {
        return -1;
    }

    // create a child process first (to get its page directory)
    process_t *child = process_create();
    if (!child)
    {
        return -1;
    }

    // load ELF into the child's page directory, recording its segments
    if (!elf_load(path, child->space->page_directory, &child->space->vmas, &elf))
    {
        process_destroy(child);
        return -1;
    }

    // the user stack area: its top pages are mapped when the process first
    // runs, the rest on demand as the stack grows down
    if (kerr_failed(vma_insert(&child->space->vmas, USER_STACK_LIMIT, USER_STACK_TOP,
                               PROT_READ | PROT_WRITE, VMA_STACK)))
    {
        process_destroy(child);
        return -1;
    }

    child->space->brk = elf.brk;
    child->parent_pid = parent ? parent->tgid : PID_NONE;
    process_set_name(child, path);

    // the child joins the caller's group and session; the first process
    // (init) leads its own of both
    child->pgid = parent ? parent->pgid : child->pid;
    child->sid = parent ? parent->sid : child->pid;
    if (parent)
    {
        process_copy_credentials(child, parent);
    }
    apply_set_ids(child, &program);

    // deep-copy argv into kernel heap (survives until trampoline consumes it)
    child->argv = argv_copy(argv);

    // inherit parent's working directory and file descriptor table
    if (parent)
    {
        strcpy(child->fs->cwd, parent->fs->cwd);
        strcpy(child->fs->root, parent->fs->root);

        // the child shares the parent's open files, except the ones marked
        // close-on-exec (as posix_spawn does)
        for (int i = 0; i < FD_MAX; i++)
        {
            fd_close_entry(&child->files->fd[i]);
            if (!(parent->files->fd[i].flags & FD_CLOEXEC))
            {
                fd_copy_entry(&child->files->fd[i], &parent->files->fd[i]);
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

    vfs_stat_t program;
    kerr_t allowed = check_program(path, &program);
    if (kerr_failed(allowed))
    {
        return allowed;
    }

    // load the new program into a new address space. the old one stays
    // untouched until the load has worked, so a failed execve returns.
    user_space_t *space = user_space_create();
    if (!space)
    {
        return KERR_NOMEM;
    }
    addr_space_t as = space->page_directory;

    elf_load_result_t elf;
    if (!elf_load(path, as, &space->vmas, &elf) ||
        kerr_failed(vma_insert(&space->vmas, USER_STACK_LIMIT, USER_STACK_TOP,
                               PROT_READ | PROT_WRITE, VMA_STACK)))
    {
        user_space_put(space);
        return KERR_NOENT;
    }
    space->brk = elf.brk;

    // no way back from here: swap in the new image. masked, so the process
    // is never scheduled with a half-changed address space.
    uint32_t flags = arch_irq_save();
    user_space_t *old = proc->space;
    proc->space = space;
    process_set_name(proc, path);
    signal_exec(proc);
    apply_set_ids(proc, &program);

    // the new program starts with clean FPU/SSE registers
    arch_fpu_clean(proc->fpu_state);
    arch_fpu_restore(proc->fpu_state);
    arch_switch_address_space(as);
    arch_irq_restore(flags);

    user_space_put(old);

    // descriptors marked close-on-exec do not survive into the new program
    for (int i = 0; i < FD_MAX; i++)
    {
        if (proc->files->fd[i].flags & FD_CLOEXEC)
        {
            fd_close_entry(&proc->files->fd[i]);
        }
    }

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

/**
 * end the current thread with a wait status (wait_status.h). never
 * returns. the last thread of a group ends the group: its children go to
 * init, and the parent gets SIGCHLD and the status. a thread that is not
 * the leader is reaped by the kernel; the leader stays a zombie until the
 * group has ended and the parent reaps it.
 */
static void thread_exit(int32_t status) __attribute__((noreturn));

static void thread_exit(int32_t status)
{
    process_t *proc = process_current();
    process_t *leader = proc->leader;

    // while the group ends, each thread ends with the group's status
    uint32_t flags = arch_irq_save();
    if (leader->group_exiting)
    {
        status = leader->group_status;
    }
    bool last = --leader->threads == 0;
    arch_irq_restore(flags);

    if (last && proc->tgid == PID_INIT)
    {
        // PID 1 (init, for now /bin/sh) has no parent to reap it. with no
        // init left the system cannot do anything more: halt.
        arch_disable_interrupts();
        KLOG_WARN("kernel: init ended with wait status 0x%x, system halted", (uint32_t)status);
        printf("init ended (wait status 0x%x). System halted.\r\n", (uint32_t)status);
        arch_halt_forever();
    }

    // CLONE_CHILD_CLEARTID: the thread's id word becomes 0, for a thread
    // that waits for this one to end (pthread_join). a bad address is
    // the program's own fault: nothing is written then.
    if (proc->clear_tid)
    {
        uint32_t zero = 0;
        (void)copy_to_user((void *)proc->clear_tid, &zero, sizeof(zero));
    }

    // release everything but the kernel stack and slot now, while this
    // thread can still sleep (closing a file may wait for the fs mutex).
    // closing the fds here also gives pipe readers EOF as soon as the
    // writer exits, not when the parent gets round to reaping it.
    process_release_resources(proc);

    // the group's children outlive it: zombie ones are destroyed now,
    // running ones are reaped by init or the kernel when they exit
    if (last)
    {
        process_orphan_children(proc);
    }

    // masked from here on: becoming a zombie, waking the parent and
    // switching away must not be split by a timer tick. this context never
    // resumes, so the state is never restored.
    arch_disable_interrupts();

    proc->state = PROC_STATE_ZOMBIE;
    proc->exit_status = status;
    scheduler_remove(proc);

    if (proc != leader)
    {
        // a thread is nobody's child: the scheduler reaps it once it's gone
        proc->parent_pid = PID_NONE;
        process_note_orphan_zombie();
    }

    if (last)
    {
        leader->exit_status = status;
        // parent_pid is PID_NONE if the parent exited first
        process_t *parent = process_get(leader->parent_pid);
        if (parent)
        {
            // a parent in waitpid sleeps on its leader's child_exit queue
            signal_send(parent, SIGCHLD);
            wait_queue_wake_all(&parent->child_exit);
        }
        else
        {
            // nobody will waitpid for the group: the kernel reaps its leader
            process_note_orphan_zombie();
        }
    }

    scheduler_yield();

    // unreachable
    kernel_panic("thread_exit: a zombie woke up... run.");
}

/**
 * end every thread of the current group with a wait status, the caller
 * included. the first status set holds. the other threads get SIGKILL,
 * which wakes them from a sleep or a stop; they end with the group's
 * status when they next return to user mode (signal_deliver).
 */
static void group_exit(int32_t status) __attribute__((noreturn));

static void group_exit(int32_t status)
{
    process_t *proc = process_current();
    process_t *leader = proc->leader;

    uint32_t flags = arch_irq_save();
    if (!leader->group_exiting)
    {
        leader->group_exiting = true;
        leader->group_status = status;
    }
    arch_irq_restore(flags);

    if (leader->threads > 1)
    {
        process_signal_threads(proc, SIGKILL);
    }
    thread_exit(status);
}

void usermode_exit(int exit_code)
{
    thread_exit(WAIT_STATUS_EXITED(exit_code));
}

void usermode_exit_group(int exit_code)
{
    group_exit(WAIT_STATUS_EXITED(exit_code));
}

void usermode_kill(int signal)
{
    group_exit(WAIT_STATUS_SIGNALED(signal));
}

void usermode_end_thread(void)
{
    thread_exit(process_current()->leader->group_status);
}

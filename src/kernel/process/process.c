#include "process.h"
#include "kernel/memory/kstack.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/memory_layout.h"
#include "arch/arch.h"
#include "arch/arch_context.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/process/wait_status.h"
#include "kernel/sync/spinlock.h"
#include "kernel/memory/heap.h"
#include "kernel/assert.h"
#include "common/memory.h"

// the process table fixed array of PCBs.
static process_t proc_table[PROCESS_MAX];

// next PID to assign (monotonically increasing).
static uint32_t next_pid = 1;

// pointer to the currently running process.
static process_t *current_process = NULL;

// orphan zombies waiting for process_reap_orphans (changed with IRQs masked)
static volatile uint32_t orphan_zombies;

// guards slot ownership (a slot's state leaving or entering UNUSED, and its
// pid) and next_pid, so two processes in exec at once can't claim the same
// slot or pid, and a lookup never sees a half-freed slot.
static spinlock_t table_lock;

/**
 * claim a free slot and give it a fresh pid.
 * @return the claimed slot in PROC_STATE_NEW, or NULL if the table is full.
 */
static process_t *claim_slot(void)
{
    process_t *proc = NULL;

    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        if (proc_table[i].state == PROC_STATE_UNUSED)
        {
            proc = &proc_table[i];
            memset(proc, 0, sizeof(process_t));
            proc->pid = next_pid++;
            proc->state = PROC_STATE_NEW;
            break;
        }
    }
    spin_unlock_irqrestore(&table_lock, flags);

    return proc;
}

/**
 * return a slot to the table. its resources must already be freed.
 */
static void release_slot(process_t *proc)
{
    uint32_t flags = spin_lock_irqsave(&table_lock);
    proc->pid = PID_NONE;
    proc->state = PROC_STATE_UNUSED;
    spin_unlock_irqrestore(&table_lock, flags);
}

void process_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    next_pid = 1;
    current_process = NULL;
    spinlock_init(&table_lock);
}

process_t *process_create(void)
{
    process_t *proc = claim_slot();
    if (!proc)
    {
        return NULL; /* table full */
    }

    // the slot is ours (PROC_STATE_NEW), so it is set up without the lock
    proc->parent_pid = PID_NONE;
    proc->brk = 0;
    vma_table_init(&proc->vmas);
    proc->exit_status = 0;
    wait_queue_init(&proc->child_exit);

    // allocate a guarded kernel stack (zeroed)
    uintptr_t stack = kstack_alloc();
    if (!stack)
    {
        release_slot(proc);
        return NULL;
    }
    proc->kernel_stack = stack;
    // ESP starts at the top of the stack (grows downward)
    proc->kernel_esp = stack + KERNEL_STACK_SIZE;

    // allocate a per-process address space (shares kernel mappings)
    addr_space_t pd = arch_create_address_space();
    if (pd == ADDR_SPACE_NONE)
    {
        kstack_free(stack);
        release_slot(proc);
        return NULL;
    }
    proc->page_directory = pd;

    // initialize fd table: stdin/stdout/stderr as console
    for (int fd = 0; fd < FD_STDIO_COUNT; fd++)
    {
        proc->fds[fd].file = file_console();
    }

    // default working directory
    proc->cwd[0] = '/';
    proc->cwd[1] = '\0';

    return proc;
}

process_t *process_fork(const struct registers *regs)
{
    process_t *parent = current_process;
    if (!parent)
    {
        return NULL;
    }

    process_t *child = process_create();
    if (!child)
    {
        return NULL;
    }

    // the memory: the same areas, and the parent's pages shared
    // copy-on-write
    if (!arch_copy_user_space(child->page_directory, parent->page_directory))
    {
        process_destroy(child);
        return NULL;
    }
    child->vmas = parent->vmas;
    child->brk = parent->brk;

    child->parent_pid = parent->pid;
    child->tty_raw = parent->tty_raw;
    memcpy(child->name, parent->name, sizeof(child->name));
    signal_fork(child, parent);
    memcpy(child->cwd, parent->cwd, sizeof(child->cwd));

    // every fd is inherited: the child's descriptors share the parent's
    // open files (and so their offsets)
    for (int i = 0; i < FD_MAX; i++)
    {
        fd_close_entry(&child->fds[i]);
        fd_copy_entry(&child->fds[i], &parent->fds[i]);
    }

    // the child resumes from the same syscall, which returns 0 there
    arch_setup_fork_stack(child, regs);
    scheduler_ready(child);
    return child;
}

process_t *process_current(void)
{
    return current_process;
}

void process_set_current(process_t *proc)
{
    current_process = proc;
}

/**
 * free a kernel-heap argv copy (an array of strings, NULL-terminated).
 */
static void free_argv(char **argv)
{
    if (!argv)
    {
        return;
    }

    for (uint32_t i = 0; argv[i]; i++)
    {
        kfree(argv[i]);
    }
    kfree(argv);
}

void process_release_resources(process_t *proc)
{
    // close all open file descriptors (including pipes and fds 0-2). this
    // may run for a process that isn't the current one (a parent reaping a
    // child that never ran), so it works on the entries directly rather
    // than through fd_close(), which operates on process_current().
    for (int i = 0; i < FD_MAX; i++)
    {
        fd_close_entry(&proc->fds[i]);
    }

    free_argv(proc->argv);
    proc->argv = NULL;

    // free the per-process address space (and user page tables/frames).
    // a process tearing down its own space moves to the kernel's first; its
    // kernel stack lives in the shared kernel region, so it stays mapped.
    if (proc->page_directory)
    {
        if (proc == current_process)
        {
            arch_switch_address_space(arch_kernel_address_space());
        }
        arch_destroy_address_space(proc->page_directory);
        proc->page_directory = 0;
    }
    vma_table_init(&proc->vmas);
}

void process_destroy(process_t *proc)
{
    if (!proc)
    {
        return;
    }

    process_release_resources(proc);

    // free the kernel stack
    if (proc->kernel_stack)
    {
        kstack_free(proc->kernel_stack);
        proc->kernel_stack = 0;
    }

    release_slot(proc);
}

void process_orphan_children(process_t *parent)
{
    process_t *zombies[PROCESS_MAX];
    uint32_t zombie_count = 0;

    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        process_t *child = &proc_table[i];
        if (child->state == PROC_STATE_UNUSED || child->parent_pid != parent->pid)
        {
            continue;
        }

        if (child->state == PROC_STATE_ZOMBIE)
        {
            // exited and never reaped: nobody else will, so destroy it below
            zombies[zombie_count++] = child;
        }
        else
        {
            // still running: the kernel reaps it when it exits
            child->parent_pid = PID_NONE;
        }
    }
    spin_unlock_irqrestore(&table_lock, flags);

    // zombies have switched away for good, so their stacks are free to go
    for (uint32_t i = 0; i < zombie_count; i++)
    {
        process_destroy(zombies[i]);
    }
}

void process_note_orphan_zombie(void)
{
    KASSERT(!arch_irqs_enabled(), "process_note_orphan_zombie: interrupts enabled");
    orphan_zombies++;
}

void process_reap_orphans(void)
{
    if (orphan_zombies == 0)
    {
        return;
    }

    for (int i = 0; i < PROCESS_MAX; i++)
    {
        process_t *proc = &proc_table[i];
        if (proc->state == PROC_STATE_ZOMBIE && proc->parent_pid == PID_NONE &&
            proc != current_process)
        {
            // an orphan released everything else before it exited, so only
            // its stack and slot are left and nothing here can sleep
            KASSERT(proc->page_directory == 0, "orphan zombie still has an address space");
            process_destroy(proc);
            orphan_zombies--;
        }
    }
}

process_t *process_find_waitable_child(process_t *parent, int32_t pid, uint32_t options,
                                       bool *has_child)
{
    process_t *found = NULL;
    *has_child = false;

    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        process_t *child = &proc_table[i];
        if (child->state == PROC_STATE_UNUSED || child->parent_pid != parent->pid ||
            (pid != -1 && child->pid != (uint32_t)pid))
        {
            continue;
        }
        *has_child = true;
        if (child->state == PROC_STATE_ZOMBIE ||
            ((options & WUNTRACED) && child->wait_report == WAIT_REPORT_STOPPED) ||
            ((options & WCONTINUED) && child->wait_report == WAIT_REPORT_CONTINUED))
        {
            found = child;
            break;
        }
    }
    spin_unlock_irqrestore(&table_lock, flags);
    return found;
}

void process_for_each(void (*fn)(process_t *proc, void *ctx), void *ctx)
{
    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        if (proc_table[i].state != PROC_STATE_UNUSED)
        {
            fn(&proc_table[i], ctx);
        }
    }
    spin_unlock_irqrestore(&table_lock, flags);
}

uint32_t process_list_pids(uint32_t *pids, uint32_t max)
{
    uint32_t count = 0;
    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX && count < max; i++)
    {
        if (proc_table[i].state != PROC_STATE_UNUSED)
        {
            pids[count++] = proc_table[i].pid;
        }
    }
    spin_unlock_irqrestore(&table_lock, flags);
    return count;
}

bool process_info(uint32_t pid, process_info_t *info)
{
    bool found = false;
    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        process_t *proc = &proc_table[i];
        if (proc->state == PROC_STATE_UNUSED || proc->pid != pid)
        {
            continue;
        }

        // copied with the table locked: the slot cannot be freed meanwhile
        info->pid = proc->pid;
        info->parent_pid = proc->parent_pid;
        info->state = proc->state;
        memcpy(info->name, proc->name, sizeof(info->name));
        info->areas = proc->vmas.count;
        info->open_fds = 0;
        for (int fd = 0; fd < FD_MAX; fd++)
        {
            info->open_fds += proc->fds[fd].file != NULL;
        }
        info->brk = proc->brk;
        found = true;
        break;
    }
    spin_unlock_irqrestore(&table_lock, flags);
    return found;
}

void process_set_name(process_t *proc, const char *path)
{
    const char *name = path;
    for (const char *c = path; *c; c++)
    {
        if (*c == '/')
        {
            name = c + 1;
        }
    }

    uint32_t i = 0;
    for (; name[i] && i < PROCESS_NAME_MAX - 1; i++)
    {
        proc->name[i] = name[i];
    }
    proc->name[i] = '\0';
}

process_t *process_get(uint32_t pid)
{
    if (pid == PID_NONE)
    {
        return NULL;
    }

    process_t *found = NULL;

    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        if (proc_table[i].state != PROC_STATE_UNUSED && proc_table[i].pid == pid)
        {
            found = &proc_table[i];
            break;
        }
    }
    spin_unlock_irqrestore(&table_lock, flags);

    return found;
}

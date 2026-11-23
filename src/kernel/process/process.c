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
#include "common/string.h"

// the process table. a slot is made on the heap the first time it is
// needed and is kept for reuse after its process ends, so a process
// pointer stays valid and only the most processes there were at once take
// memory. a NULL slot is unused.
static process_t *proc_table[PROCESS_MAX];

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
    // a new slot is made before the lock is taken; it goes back if an old
    // slot is free. the heap gives 4-byte alignment, and fpu_state needs 16
    // (fxsave): take 15 more bytes and round up (a slot is never freed)
    void *raw = kcalloc(1, sizeof(process_t) + 15);
    process_t *spare = raw ? (process_t *)(((uintptr_t)raw + 15) & ~(uintptr_t)15) : NULL;
    process_t *proc = NULL;

    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX && !proc; i++)
    {
        if (proc_table[i] && proc_table[i]->state == PROC_STATE_UNUSED)
        {
            proc = proc_table[i];
        }
    }
    for (int i = 0; i < PROCESS_MAX && !proc && spare; i++)
    {
        if (!proc_table[i])
        {
            proc_table[i] = spare;
            proc = spare;
            spare = NULL;
        }
    }
    if (proc)
    {
        memset(proc, 0, sizeof(process_t));
        proc->pid = next_pid++;
        proc->tgid = proc->pid; // a group of its own until clone joins one
        proc->leader = proc;
        proc->threads = 1;
        proc->state = PROC_STATE_NEW;
    }
    spin_unlock_irqrestore(&table_lock, flags);

    if (spare)
    {
        kfree(raw);
    }
    return proc;
}

/**
 * return a slot to the table. its resources must already be freed.
 */
static void release_slot(process_t *proc)
{
    fd_table_put(proc->files); // a process that failed before it ran
    proc->files = NULL;
    signal_handlers_put(proc->handlers); // a zombie can still get signals
    proc->handlers = NULL;
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

/**
 * claim a slot and give it a kernel stack: a process without memory,
 * descriptors, fs context or handlers yet.
 * @return the slot in PROC_STATE_NEW, or NULL.
 */
static process_t *create_task(void)
{
    process_t *proc = claim_slot();
    if (!proc)
    {
        return NULL; // table full
    }

    // the slot is ours (PROC_STATE_NEW), so it is set up without the lock
    proc->parent_pid = PID_NONE;
    proc->exit_status = 0;
    wait_queue_init(&proc->child_exit);
    arch_fpu_clean(proc->fpu_state);

    // a guarded kernel stack (zeroed); ESP starts at its top
    uintptr_t stack = kstack_alloc();
    if (!stack)
    {
        release_slot(proc);
        return NULL;
    }
    proc->kernel_stack = stack;
    proc->kernel_esp = stack + KERNEL_STACK_SIZE;
    return proc;
}

process_t *process_create(void)
{
    process_t *proc = create_task();
    if (!proc)
    {
        return NULL;
    }

    // its own user memory (the kernel half is shared by all), descriptors
    // (0 to 2 are the console), handlers (all SIG_DFL), and fs context
    proc->space = user_space_create();
    proc->files = fd_table_create();
    proc->handlers = signal_handlers_create();
    proc->fs = kcalloc(1, sizeof(fs_context_t));
    if (!proc->space || !proc->files || !proc->handlers || !proc->fs)
    {
        process_destroy(proc);
        return NULL;
    }
    for (int fd = 0; fd < FD_STDIO_COUNT; fd++)
    {
        proc->files->fd[fd].file = file_console();
    }
    proc->fs->users = 1;
    proc->fs->umask = 022; // the ids start as root's (0), from the cleared slot
    strcpy(proc->fs->cwd, "/");
    strcpy(proc->fs->root, "/");
    return proc;
}

process_t *process_clone_thread(const struct registers *regs, uintptr_t stack)
{
    process_t *parent = current_process;
    process_t *thread = create_task();
    if (!thread)
    {
        return NULL;
    }

    // one group: the memory, the descriptors, the fs context and the
    // handlers are the same objects
    thread->space = user_space_get(parent->space);
    thread->files = fd_table_get(parent->files);
    thread->fs = fs_context_get(parent->fs);
    thread->handlers = signal_handlers_get(parent->handlers);

    thread->tgid = parent->tgid;
    thread->leader = parent->leader;
    thread->parent_pid = parent->parent_pid; // the group's parent
    thread->pgid = parent->pgid;
    thread->sid = parent->sid;
    process_copy_credentials(thread, parent);
    memcpy(thread->name, parent->name, sizeof(thread->name));
    thread->signals.blocked = parent->signals.blocked;
    arch_fpu_save(thread->fpu_state);

    // the thread returns from the same syscall with 0, on its own stack
    arch_setup_fork_stack(thread, regs, stack);

    uint32_t flags = arch_irq_save();
    thread->leader->threads++;
    arch_irq_restore(flags);
    return thread;
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
    if (!arch_copy_user_space(child->space->page_directory, parent->space->page_directory))
    {
        process_destroy(child);
        return NULL;
    }
    child->space->vmas = parent->space->vmas;
    child->space->brk = parent->space->brk;

    child->parent_pid = parent->tgid; // a child of the group, not the thread
    child->pgid = parent->pgid;
    child->sid = parent->sid;
    process_copy_credentials(child, parent);
    memcpy(child->name, parent->name, sizeof(child->name));
    signal_fork(child, parent);
    memcpy(child->fs->cwd, parent->fs->cwd, sizeof(child->fs->cwd));
    memcpy(child->fs->root, parent->fs->root, sizeof(child->fs->root));

    // every fd is inherited: the child's descriptors share the parent's
    // open files (and so their offsets)
    for (int i = 0; i < FD_MAX; i++)
    {
        fd_close_entry(&child->files->fd[i]);
        fd_copy_entry(&child->files->fd[i], &parent->files->fd[i]);
    }

    // the child's FPU registers are the parent's, as they are now in the CPU
    arch_fpu_save(child->fpu_state);

    // the child resumes from the same syscall, which returns 0 there
    arch_setup_fork_stack(child, regs, 0);
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
    // let go of the descriptors; the last user closes them (pipes and
    // fds 0-2 included). this may run for a process that isn't the
    // current one: a parent reaping a child that never ran.
    fd_table_put(proc->files);
    proc->files = NULL;

    free_argv(proc->argv);
    proc->argv = NULL;

    // let go of the user memory; the last user frees it (a process that
    // frees its own moves to the kernel's page tables first)
    user_space_put(proc->space);
    proc->space = NULL;

    fs_context_put(proc->fs);
    proc->fs = NULL;
}

fs_context_t *fs_context_get(fs_context_t *fs)
{
    uint32_t flags = arch_irq_save();
    fs->users++;
    arch_irq_restore(flags);
    return fs;
}

void fs_context_put(fs_context_t *fs)
{
    if (!fs)
    {
        return;
    }
    uint32_t flags = arch_irq_save();
    bool last = --fs->users == 0;
    arch_irq_restore(flags);
    if (last)
    {
        kfree(fs);
    }
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

/**
 * true if PID 1 exists and has not exited. call with the table locked.
 */
static bool process_init_alive(void)
{
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        process_t *proc = proc_table[i];
        if (!proc)
        {
            continue;
        }
        if (proc->pid == PID_INIT && proc->state != PROC_STATE_UNUSED &&
            proc->state != PROC_STATE_ZOMBIE)
        {
            return true;
        }
    }
    return false;
}

void process_orphan_children(process_t *parent)
{
    process_t *zombies[PROCESS_MAX];
    uint32_t zombie_count = 0;

    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        process_t *child = proc_table[i];
        if (!child)
        {
            continue;
        }
        if (child->state == PROC_STATE_UNUSED || child->parent_pid != parent->tgid)
        {
            continue;
        }

        if (child->state == PROC_STATE_ZOMBIE && child->threads == 0)
        {
            // a group that ended and was never reaped: nobody else will, so
            // destroy it below
            zombies[zombie_count++] = child;
        }
        else if (parent->tgid != PID_INIT && process_init_alive())
        {
            // still running: init adopts it and reaps it when it exits
            child->parent_pid = PID_INIT;
        }
        else
        {
            // no init to adopt it: the kernel reaps it when it exits
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
        process_t *proc = proc_table[i];
        if (!proc)
        {
            continue;
        }
        if (proc->state == PROC_STATE_ZOMBIE && proc->parent_pid == PID_NONE &&
            proc != current_process)
        {
            // an orphan released everything else before it exited, so only
            // its stack and slot are left and nothing here can sleep
            KASSERT(!proc->space, "orphan zombie still has an address space");
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
        process_t *child = proc_table[i];
        if (!child)
        {
            continue;
        }
        // a child is a group: its leader stands for it
        if (child->state == PROC_STATE_UNUSED || child->parent_pid != parent->tgid ||
            child->pid != child->tgid || (pid != -1 && child->pid != (uint32_t)pid))
        {
            continue;
        }
        *has_child = true;
        if ((child->state == PROC_STATE_ZOMBIE && child->threads == 0) ||
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

uint32_t process_list_group(uint32_t pgid, uint32_t *pids, uint32_t max)
{
    uint32_t count = 0;
    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX && count < max; i++)
    {
        process_t *proc = proc_table[i];
        if (!proc)
        {
            continue;
        }
        if (proc->state != PROC_STATE_UNUSED && proc->state != PROC_STATE_ZOMBIE &&
            proc->pgid == pgid)
        {
            pids[count++] = proc->pid;
        }
    }
    spin_unlock_irqrestore(&table_lock, flags);
    return count;
}

bool process_group_exists(uint32_t pgid, uint32_t sid)
{
    bool found = false;
    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX && !found; i++)
    {
        process_t *proc = proc_table[i];
        if (!proc)
        {
            continue;
        }
        found = proc->state != PROC_STATE_UNUSED && proc->state != PROC_STATE_ZOMBIE &&
                proc->pgid == pgid && proc->sid == sid;
    }
    spin_unlock_irqrestore(&table_lock, flags);
    return found;
}

void process_for_each(void (*fn)(process_t *proc, void *ctx), void *ctx)
{
    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        if (proc_table[i] && proc_table[i]->state != PROC_STATE_UNUSED)
        {
            fn(proc_table[i], ctx);
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
        if (proc_table[i] && proc_table[i]->state != PROC_STATE_UNUSED)
        {
            pids[count++] = proc_table[i]->pid;
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
        process_t *proc = proc_table[i];
        if (!proc)
        {
            continue;
        }
        if (proc->state == PROC_STATE_UNUSED || proc->pid != pid)
        {
            continue;
        }

        // copied with the table locked: the slot cannot be freed meanwhile
        info->pid = proc->pid;
        info->parent_pid = proc->parent_pid;
        info->state = proc->state;
        memcpy(info->name, proc->name, sizeof(info->name));
        info->areas = proc->space ? proc->space->vmas.count : 0;
        info->open_fds = 0;
        for (int fd = 0; proc->files && fd < FD_MAX; fd++)
        {
            info->open_fds += proc->files->fd[fd].file != NULL;
        }
        info->brk = proc->space ? proc->space->brk : 0;
        info->uid = proc->uid;
        info->utime = proc->utime;
        info->stime = proc->stime;
        info->pages = proc->space ? arch_count_user_pages(proc->space->page_directory) : 0;
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

static uint64_t idle_ticks;

uint64_t process_idle_ticks(void)
{
    return idle_ticks;
}

void process_account_tick(bool user)
{
    process_t *proc = current_process;
    if (!proc || proc->pid == PID_NONE)
    {
        idle_ticks++; // the idle loop
        return;
    }
    if (user)
    {
        proc->utime++;
    }
    else
    {
        proc->stime++;
    }
}

void process_copy_credentials(process_t *child, const process_t *parent)
{
    child->uid = parent->uid;
    child->euid = parent->euid;
    child->suid = parent->suid;
    child->gid = parent->gid;
    child->egid = parent->egid;
    child->sgid = parent->sgid;
    child->fs->umask = parent->fs->umask;
    child->nice = parent->nice;
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
        if (proc_table[i] && proc_table[i]->state != PROC_STATE_UNUSED &&
            proc_table[i]->pid == pid)
        {
            found = proc_table[i];
            break;
        }
    }
    spin_unlock_irqrestore(&table_lock, flags);

    return found;
}

void process_signal_threads(process_t *proc, int sig)
{
    process_t *threads[PROCESS_MAX];
    uint32_t count = 0;
    uint32_t flags = spin_lock_irqsave(&table_lock);
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        process_t *other = proc_table[i];
        if (other && other != proc && other->tgid == proc->tgid &&
            other->state != PROC_STATE_UNUSED && other->state != PROC_STATE_ZOMBIE)
        {
            threads[count++] = other;
        }
    }
    spin_unlock_irqrestore(&table_lock, flags);

    // a slot is never freed, so a thread that ends meanwhile is only
    // skipped by signal_send
    for (uint32_t i = 0; i < count; i++)
    {
        signal_send(threads[i], sig);
    }
}

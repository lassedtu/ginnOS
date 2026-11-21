#include "kernel/sync/wait_queue.h"
#include "kernel/scheduler/scheduler.h"
#include "arch/arch.h"
#include "kernel/assert.h"

/**
 * append a process to the tail of the queue. the list is singly-linked
 * and short (usually one waiter), so an O(n) tail walk is fine and avoids
 * carrying a tail pointer. caller must hold interrupts disabled.
 */
static void queue_append(wait_queue_t *wq, process_t *proc)
{
    proc->wait_next = NULL;

    if (!wq->head)
    {
        wq->head = proc;
        return;
    }

    process_t *node = wq->head;
    while (node->wait_next)
    {
        node = node->wait_next;
    }
    node->wait_next = proc;
}

void wait_queue_init(wait_queue_t *wq)
{
    wq->head = NULL;
}

/**
 * put the current process to sleep on the queue and run something else.
 * returns once another context has woken it. caller must hold interrupts
 * disabled, so a wake cannot land between enqueuing and yielding.
 */
static void block_current(wait_queue_t *wq, process_t *proc, bool interruptible)
{
    // without a running scheduler the yield below returns at once, and the
    // caller's retry would queue this process a second time
    KASSERT(scheduler_is_active(), "wait queue: blocking before the scheduler runs");

    proc->state = PROC_STATE_BLOCKED;
    proc->waiting_on = wq;
    proc->interruptible = interruptible;
    scheduler_remove(proc);
    queue_append(wq, proc);

    // the yield runs the next process; when we are later woken and
    // rescheduled, execution resumes here.
    scheduler_yield();
}

/**
 * block_current for the current process, interrupts masked around it.
 */
static void block_masked(wait_queue_t *wq, bool interruptible)
{
    process_t *proc = process_current();
    if (!proc)
    {
        return;
    }

    uint32_t flags = arch_irq_save();
    block_current(wq, proc, interruptible);
    arch_irq_restore(flags);
}

void wait_queue_block(wait_queue_t *wq)
{
    block_masked(wq, false);
}

void wait_queue_block_interruptible(wait_queue_t *wq)
{
    block_masked(wq, true);
}

/**
 * block_current with a spinlock released while asleep.
 */
static void block_locked(wait_queue_t *wq, spinlock_t *lock, bool interruptible)
{
    process_t *proc = process_current();

    KASSERT(!arch_irqs_enabled(), "wait_queue_block_locked: interrupts enabled");
    KASSERT(lock->locked == 1, "wait_queue_block_locked: lock not held");

    if (!proc)
    {
        return;
    }

    // interrupts stay masked across the release, so the lock only has to
    // be marked free for whoever runs while this process sleeps.
    lock->locked = 0;
    block_current(wq, proc, interruptible);
    lock->locked = 1;
}

void wait_queue_block_locked(wait_queue_t *wq, spinlock_t *lock)
{
    block_locked(wq, lock, false);
}

void wait_queue_block_locked_interruptible(wait_queue_t *wq, spinlock_t *lock)
{
    block_locked(wq, lock, true);
}

bool wait_queue_interrupt(process_t *proc)
{
    KASSERT(!arch_irqs_enabled(), "wait_queue_interrupt: interrupts enabled");

    wait_queue_t *wq = proc->waiting_on;
    if (proc->state != PROC_STATE_BLOCKED || !wq || !proc->interruptible)
    {
        return false;
    }

    process_t **link = &wq->head;
    while (*link && *link != proc)
    {
        link = &(*link)->wait_next;
    }
    if (*link)
    {
        *link = proc->wait_next;
    }
    proc->wait_next = NULL;
    proc->waiting_on = NULL;
    scheduler_ready(proc);
    return true;
}

void wait_queue_wake_one(wait_queue_t *wq)
{
    uint32_t flags = arch_irq_save();

    process_t *proc = wq->head;
    if (proc)
    {
        wq->head = proc->wait_next;
        proc->wait_next = NULL;
        proc->waiting_on = NULL;
        scheduler_ready(proc);
    }

    arch_irq_restore(flags);
}

void wait_queue_wake_all(wait_queue_t *wq)
{
    uint32_t flags = arch_irq_save();

    process_t *proc = wq->head;
    while (proc)
    {
        process_t *next = proc->wait_next;
        proc->wait_next = NULL;
        proc->waiting_on = NULL;
        scheduler_ready(proc);
        proc = next;
    }
    wq->head = NULL;

    arch_irq_restore(flags);
}

#include "kernel/sync/wait_queue.h"
#include "kernel/scheduler/scheduler.h"
#include "arch/arch.h"

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

void wait_queue_block(wait_queue_t *wq)
{
    process_t *proc = process_current();
    if (!proc)
    {
        return;
    }

    // mask interrupts so a wake from an IRQ handler can't race between us
    // enqueuing and yielding. the yield runs the next process; when we are
    // later woken and rescheduled, execution resumes here.
    uint32_t flags = arch_irq_save();

    proc->state = PROC_STATE_BLOCKED;
    scheduler_remove(proc);
    queue_append(wq, proc);

    scheduler_yield();

    // woken: we've been popped off the queue and marked ready again.
    arch_irq_restore(flags);
}

void wait_queue_wake_one(wait_queue_t *wq)
{
    uint32_t flags = arch_irq_save();

    process_t *proc = wq->head;
    if (proc)
    {
        wq->head = proc->wait_next;
        proc->wait_next = NULL;
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
        scheduler_ready(proc);
        proc = next;
    }
    wq->head = NULL;

    arch_irq_restore(flags);
}

#pragma once

/**
 * wait queue: a list of processes blocked until some event occurs.
 *
 * a process calls wait_queue_block() to give up the CPU until another
 * context wakes it with wait_queue_wake_one() or wait_queue_wake_all().
 * this replaces the busy-wait (hlt-in-a-loop) blocking that stalled the
 * whole system while one process waited on a pipe.
 *
 * used by: pipe read/write (wait for data / space), and any future
 * blocking operation (sleep, disk I/O completion).
 *
 * all operations must run with the caller in kernel context. blocking and
 * waking are done with interrupts masked, since a wake can come from an
 * interrupt handler.
 *
 * a sleep is either uninterruptible (wait_queue_block, used by mutexes:
 * the process wakes only when the event happens) or interruptible (the
 * _interruptible variants): a signal for the process also wakes it, and
 * the caller must then check signal_pending() and give up its wait.
 */

#include "kernel/sync/spinlock.h"

// forward declaration: we only store a process pointer here, so including
// process.h would create a cycle (process.h -> fd_table.h -> wait_queue.h).
struct process;

/**
 * a queue of blocked processes, linked through process_t::wait_next.
 * zero-initialized (head == NULL) means empty.
 */
typedef struct
{
    struct process *head; // first waiter, or NULL if none
} wait_queue_t;

/**
 * initialize a wait queue to empty.
 * @param wq the queue to initialize.
 */
void wait_queue_init(wait_queue_t *wq);

/**
 * block the current process on this queue and yield the CPU.
 * the process is marked BLOCKED, removed from the scheduler, and appended
 * to the queue. returns only once another context has woken it, at which
 * point it is runnable again.
 * @param wq the queue to block on.
 */
void wait_queue_block(wait_queue_t *wq);

/**
 * block on this queue while holding a spinlock, releasing the lock for as
 * long as the process sleeps and holding it again on return.
 *
 * this is the check-then-sleep pattern: take the lock, test a condition,
 * and if it is not met call this. interrupts stay masked from the test
 * until the process is on the queue, so a wake from another process or an
 * IRQ handler cannot slip in between and be lost. callers re-test the
 * condition in a loop after waking.
 * @param wq the queue to block on.
 * @param lock a lock taken with spin_lock_irqsave() by the caller.
 */
void wait_queue_block_locked(wait_queue_t *wq, spinlock_t *lock);

/**
 * wake the process at the head of the queue, if any.
 * the woken process is marked READY and handed back to the scheduler.
 * @param wq the queue to wake from.
 */
void wait_queue_wake_one(wait_queue_t *wq);

/**
 * wake every process on the queue.
 * @param wq the queue to drain.
 */
void wait_queue_wake_all(wait_queue_t *wq);

/**
 * like wait_queue_block, but a signal for the process also wakes it. the
 * caller re-tests its condition and signal_pending() after the return.
 * @param wq the queue to block on.
 */
void wait_queue_block_interruptible(wait_queue_t *wq);

/**
 * like wait_queue_block_locked, but a signal for the process also wakes
 * it. the caller re-tests its condition and signal_pending() after the
 * return.
 * @param wq the queue to block on.
 * @param lock a lock taken with spin_lock_irqsave() by the caller.
 */
void wait_queue_block_locked_interruptible(wait_queue_t *wq, spinlock_t *lock);

/**
 * take a process out of the queue it sleeps on, if it sleeps
 * interruptibly, and make it runnable (a signal arrived). call with
 * interrupts masked.
 * @param proc the process.
 * @return true if it was asleep and is now woken.
 */
bool wait_queue_interrupt(struct process *proc);

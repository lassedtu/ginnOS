/**
 * @file futex.c
 * @brief futex wait and wake (futex.h), and SYS_futex.
 *
 * the waiters sleep on one of a few timed queues, chosen by the futex
 * address. a wake takes the waiters of its futex off the queue and leaves
 * the others. a waiter with a timeout also wakes when the queue's
 * deadline comes (with the others on that queue, which sleep again).
 *
 * the kernel has one CPU: masked interrupts keep the check of the word
 * and the sleep together, so a wake between the two cannot be lost.
 */

#include "futex.h"

#include "kernel/process/process.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/signal/signal.h"
#include "kernel/syscall/syscall_internal.h"
#include "kernel/time/clock.h"
#include "kernel/time/timer.h"
#include "arch/arch.h"

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128

#define FUTEX_QUEUES 16

static timed_queue_t queues[FUTEX_QUEUES];

static timed_queue_t *queue_for(uintptr_t uaddr)
{
    return &queues[(uaddr >> 2) % FUTEX_QUEUES];
}

void futex_init(void)
{
    for (int i = 0; i < FUTEX_QUEUES; i++)
    {
        timed_queue_init(&queues[i]);
    }
}

int futex_wake(struct user_space *space, uintptr_t uaddr, int count)
{
    wait_queue_t *waiters = &queue_for(uaddr)->waiters;
    int woken = 0;

    uint32_t flags = arch_irq_save();
    process_t **link = &waiters->head;
    while (*link && woken < count)
    {
        process_t *proc = *link;
        if (proc->futex_space != space || proc->futex_addr != uaddr)
        {
            link = &proc->wait_next;
            continue;
        }
        *link = proc->wait_next;
        proc->wait_next = NULL;
        proc->waiting_on = NULL;
        proc->futex_woken = true;
        scheduler_ready(proc);
        woken++;
    }
    arch_irq_restore(flags);
    return woken;
}

/**
 * sleep on the futex at uaddr while it holds expected.
 * @param deadline the tick to give up at, or TIMER_NEVER.
 * @return 0 (woken), -EAGAIN (the word was not expected), -ETIMEDOUT,
 *         -EINTR, or -EFAULT.
 */
static int32_t futex_wait(uintptr_t uaddr, uint32_t expected, uint64_t deadline)
{
    process_t *proc = process_current();
    timed_queue_t *queue = queue_for(uaddr);
    int32_t result = 0;

    uint32_t flags = arch_irq_save();
    uint32_t value;
    if (kerr_failed(copy_from_user(&value, (const void *)uaddr, sizeof(value))))
    {
        result = -EFAULT;
    }
    else if (value != expected)
    {
        result = -EAGAIN;
    }
    else
    {
        proc->futex_space = proc->space;
        proc->futex_addr = uaddr;
        proc->futex_woken = false;
        for (;;)
        {
            timed_queue_block_interruptible(queue, deadline);
            if (proc->futex_woken)
            {
                break;
            }
            if (signal_pending(proc))
            {
                result = -EINTR;
                break;
            }
            if (timer_now() >= deadline)
            {
                result = -ETIMEDOUT;
                break;
            }
        }
        proc->futex_space = NULL;
        proc->futex_addr = 0;
    }
    arch_irq_restore(flags);
    return result;
}

/**
 * SYS_futex: args: uaddr (an aligned 32-bit word), op, val, timeout.
 * FUTEX_WAIT sleeps while *uaddr == val, for at most *timeout (a relative
 * struct timespec; NULL: no limit). FUTEX_WAKE wakes at most val threads
 * and returns how many it woke. FUTEX_PRIVATE_FLAG may be set: every
 * futex is private to its process here.
 * returns as above, -EINVAL (a bad address or time), -ENOSYS (another op).
 */
syscall_ret_t sys_futex(struct registers *regs)
{
    uintptr_t uaddr = syscall_arg(regs, 0);
    uint32_t op = (uint32_t)syscall_arg(regs, 1) & ~(uint32_t)FUTEX_PRIVATE_FLAG;
    uint32_t val = (uint32_t)syscall_arg(regs, 2);
    const void *user_timeout = (const void *)syscall_arg(regs, 3);

    if (uaddr & 3)
    {
        return -EINVAL;
    }

    if (op == FUTEX_WAKE)
    {
        int count = val > 0x7FFFFFFFu ? 0x7FFFFFFF : (int)val;
        return futex_wake(process_current()->space, uaddr, count);
    }
    if (op != FUTEX_WAIT)
    {
        return -ENOSYS;
    }

    uint64_t deadline = TIMER_NEVER;
    if (user_timeout)
    {
        struct ktimespec span;
        if (kerr_failed(copy_from_user(&span, user_timeout, sizeof(span))))
        {
            return -EFAULT;
        }
        if (span.tv_sec < 0 || span.tv_nsec < 0 || span.tv_nsec >= (long)NSEC_PER_SEC)
        {
            return -EINVAL;
        }
        deadline = timer_now() + clock_timespec_to_ticks(&span);
    }
    return futex_wait(uaddr, val, deadline);
}

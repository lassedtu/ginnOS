#include "timer.h"

#include "arch/arch.h"
#include "kernel/assert.h"
#include "kernel/signal/signal.h"

// every timed queue; changed with interrupts masked
static timed_queue_t *queues;

void timed_queue_init(timed_queue_t *queue)
{
    wait_queue_init(&queue->waiters);
    queue->deadline = TIMER_NEVER;

    uint32_t flags = arch_irq_save();
    queue->next = queues;
    queues = queue;
    arch_irq_restore(flags);
}

void timed_queue_block(timed_queue_t *queue, uint64_t deadline)
{
    KASSERT(!arch_irqs_enabled(), "timed_queue_block: interrupts enabled");

    if (deadline < queue->deadline)
    {
        queue->deadline = deadline;
    }
    wait_queue_block(&queue->waiters);
}

void timed_queue_block_interruptible(timed_queue_t *queue, uint64_t deadline)
{
    KASSERT(!arch_irqs_enabled(), "timed_queue_block_interruptible: interrupts enabled");

    if (deadline < queue->deadline)
    {
        queue->deadline = deadline;
    }
    wait_queue_block_interruptible(&queue->waiters);
}

void timed_queue_wake_all(timed_queue_t *queue)
{
    wait_queue_wake_all(&queue->waiters);
}

static timer_poll_fn poll_hooks[TIMER_POLL_HOOKS];

void timer_add_poll_hook(timer_poll_fn fn)
{
    for (int i = 0; i < TIMER_POLL_HOOKS; i++)
    {
        if (!poll_hooks[i])
        {
            poll_hooks[i] = fn;
            return;
        }
    }
}

void timer_tick(uint64_t now)
{
    signal_alarm_tick(now);

    for (int i = 0; i < TIMER_POLL_HOOKS && poll_hooks[i]; i++)
    {
        poll_hooks[i](now);
    }

    for (timed_queue_t *queue = queues; queue; queue = queue->next)
    {
        if (queue->deadline <= now)
        {
            // the waiters set the next earliest deadline as they sleep again
            queue->deadline = TIMER_NEVER;
            wait_queue_wake_all(&queue->waiters);
        }
    }
}

uint64_t timer_now(void)
{
    return pit_get_ticks();
}

// a tick is a whole number of milliseconds, so the conversion needs no
// 64-bit division (which the freestanding kernel has no helper for)
_Static_assert(1000 % TIMER_HZ == 0, "TIMER_HZ must divide 1000");
#define MS_PER_TICK (1000 / TIMER_HZ)

uint64_t timer_ms_to_ticks(uint32_t ms)
{
    return ms / MS_PER_TICK + (ms % MS_PER_TICK != 0);
}

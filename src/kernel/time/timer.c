#include "timer.h"

#include "arch/arch.h"
#include "kernel/assert.h"

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

void timed_queue_wake_all(timed_queue_t *queue)
{
    wait_queue_wake_all(&queue->waiters);
}

void timer_tick(uint64_t now)
{
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

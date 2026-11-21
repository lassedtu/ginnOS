#include "kernel/sync/completion.h"

#include "arch/arch.h"
#include "kernel/klog/klog.h"
#include "kernel/process/process.h"
#include "kernel/scheduler/scheduler.h"

// how long one sleep lasts before the waiter reads the status again
#define CHECK_MS 50u

// the polled wait: a spin count, because the timer may not tick (the
// caller can have interrupts off)
#define SPINS_PER_MS 20000u

// status reads before the first sleep
#define SPIN_BEFORE_SLEEP 200u

void completion_init(completion_t *completion, const char *name, bool use_irq)
{
    completion->done = false;
    completion->use_irq = use_irq;
    completion->name = name;
    timed_queue_init(&completion->queue);
}

void completion_signal(completion_t *completion)
{
    completion->done = true;
    timed_queue_wake_all(&completion->queue);
}

static bool can_sleep(void)
{
    return scheduler_is_active() && process_current() && arch_irqs_enabled();
}

static bool poll_until(completion_ready_t ready, void *context, uint32_t timeout_ms)
{
    uint64_t limit = (uint64_t)timeout_ms * SPINS_PER_MS;
    for (uint64_t spin = 0; spin < limit; spin++)
    {
        if (ready(context))
        {
            return true;
        }
    }
    return ready(context);
}

bool completion_wait_until(completion_t *completion, completion_ready_t ready, void *context,
                           uint32_t timeout_ms)
{
    // a fast device is often done within microseconds: check a few times
    // before going to sleep (a sleep and a wake cost more than that)
    for (uint32_t spin = 0; spin < SPIN_BEFORE_SLEEP; spin++)
    {
        if (ready(context))
        {
            return true;
        }
    }
    if (!completion->use_irq || !can_sleep())
    {
        return poll_until(ready, context, timeout_ms);
    }

    uint64_t deadline = timer_now() + timer_ms_to_ticks(timeout_ms);
    for (;;)
    {
        // clear and test with interrupts off: an interrupt after the test
        // wakes the sleep below, it is not lost
        uint32_t flags = arch_irq_save();
        completion->done = false;
        if (ready(context))
        {
            arch_irq_restore(flags);
            return true;
        }

        uint64_t wake = timer_now() + timer_ms_to_ticks(CHECK_MS);
        if (wake > deadline)
        {
            wake = deadline;
        }
        while (!completion->done && timer_now() < wake)
        {
            timed_queue_block(&completion->queue, wake);
        }
        bool signalled = completion->done;
        arch_irq_restore(flags);

        if (signalled)
        {
            continue;
        }
        if (ready(context))
        {
            // ready, but no interrupt came: the interrupt does not arrive
            completion->use_irq = false;
            KLOG_WARN("%s: no interrupt from the device; polling", completion->name);
            return true;
        }
        if (timer_now() >= deadline)
        {
            return false;
        }
    }
}

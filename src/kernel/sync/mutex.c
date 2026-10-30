#include "kernel/sync/mutex.h"

#include "arch/arch.h"
#include "kernel/assert.h"
#include "kernel/process/process.h"

void mutex_init(mutex_t *mutex)
{
    mutex->locked = false;
    mutex->owner = NULL;
    wait_queue_init(&mutex->waiters);
}

void mutex_lock(mutex_t *mutex)
{
    process_t *self = process_current();

    // checking and taking the mutex must not be split by a context switch,
    // and blocking must happen before the holder can release and wake.
    uint32_t flags = arch_irq_save();

    KASSERT(!mutex->locked || mutex->owner != self || self == NULL,
            "mutex_lock: already held by this process");

    // a woken waiter re-checks: another process may have taken the mutex
    // between the wakeup and this process running again.
    while (mutex->locked)
    {
        wait_queue_block(&mutex->waiters);
    }

    mutex->locked = true;
    mutex->owner = self;

    arch_irq_restore(flags);
}

void mutex_unlock(mutex_t *mutex)
{
    uint32_t flags = arch_irq_save();

    KASSERT(mutex->locked, "mutex_unlock: not locked");
    KASSERT(mutex->owner == process_current(), "mutex_unlock: not the owner");

    mutex->locked = false;
    mutex->owner = NULL;
    wait_queue_wake_one(&mutex->waiters);

    arch_irq_restore(flags);
}

bool mutex_held(const mutex_t *mutex)
{
    return mutex->locked && mutex->owner == process_current();
}

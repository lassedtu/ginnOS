#include "kernel/sync/spinlock.h"
#include "kernel/assert.h"
#include "arch/arch.h"

/**
 * on a uniprocessor kernel there is no second core to contend with, so
 * acquiring a lock just means masking interrupts: nothing else can run
 * until we release. the locked field guards against a single context
 * taking the same lock twice (a deadlock on real hardware), which KASSERT
 * catches in debug builds.
 */

void spinlock_init(spinlock_t *lock)
{
    lock->locked = 0;
}

void spin_lock(spinlock_t *lock)
{
    arch_disable_interrupts();
    KASSERT(lock->locked == 0, "spin_lock on an already-held lock");
    lock->locked = 1;
}

void spin_unlock(spinlock_t *lock)
{
    KASSERT(lock->locked == 1, "spin_unlock on a lock that isn't held");
    lock->locked = 0;
    arch_enable_interrupts();
}

uint32_t spin_lock_irqsave(spinlock_t *lock)
{
    uint32_t flags = arch_irq_save();
    KASSERT(lock->locked == 0, "spin_lock_irqsave on an already-held lock");
    lock->locked = 1;
    return flags;
}

void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags)
{
    KASSERT(lock->locked == 1, "spin_unlock_irqrestore on a lock that isn't held");
    lock->locked = 0;
    arch_irq_restore(flags);
}

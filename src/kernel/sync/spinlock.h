#pragma once

/**
 * spinlock primitive for protecting data shared between process context
 * and interrupt handlers.
 *
 * ginnOS is currently uniprocessor, so a "spin" lock never actually spins:
 * masking interrupts is enough to make a critical section atomic against
 * both preemption and IRQ handlers. the locked field is still tracked and
 * asserted so the semantics stay correct if this is ever extended to real
 * spinning on SMP.
 *
 * prefer the irqsave/irqrestore variants for any lock that an interrupt
 * handler might also take, since they restore the prior interrupt state
 * instead of blindly re-enabling interrupts.
 */

#include "common/stdint.h"

/**
 * a spinlock. zero-initialized means unlocked.
 */
typedef struct
{
    volatile uint32_t locked; // 0 = free, 1 = held
} spinlock_t;

/**
 * initialize a spinlock to the unlocked state.
 * @param lock the lock to initialize.
 */
void spinlock_init(spinlock_t *lock);

/**
 * acquire a lock, disabling interrupts for the critical section.
 * pairs with spin_unlock(). use this only when the caller knows
 * interrupts are enabled on entry; otherwise use spin_lock_irqsave().
 * @param lock the lock to acquire.
 */
void spin_lock(spinlock_t *lock);

/**
 * release a lock and re-enable interrupts.
 * pairs with spin_lock().
 * @param lock the lock to release.
 */
void spin_unlock(spinlock_t *lock);

/**
 * acquire a lock, saving the prior interrupt state before disabling it.
 * safe to nest: the returned flags carry whether interrupts were enabled
 * on entry. pairs with spin_unlock_irqrestore().
 * @param lock the lock to acquire.
 * @return the interrupt state to hand back to spin_unlock_irqrestore().
 */
uint32_t spin_lock_irqsave(spinlock_t *lock);

/**
 * release a lock and restore the interrupt state captured at acquire time.
 * pairs with spin_lock_irqsave().
 * @param lock the lock to release.
 * @param flags the value returned by spin_lock_irqsave().
 */
void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags);

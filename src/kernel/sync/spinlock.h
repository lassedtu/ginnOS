#pragma once

/**
 * kernel locking rules (uniprocessor):
 *
 * - syscalls run with interrupts enabled (int 0x80 is a trap gate) and the
 *   timer can switch processes anywhere in kernel code, so any data shared
 *   between processes needs a lock, not just data shared with IRQs.
 * - IRQ handlers run with interrupts masked and must never sleep.
 * - short critical sections, and anything an IRQ handler touches: a
 *   spinlock with spin_lock_irqsave / spin_unlock_irqrestore. examples:
 *   heap, pmm, process table, pipes, keyboard ring, console.
 * - long sections in process context (disk and filesystem I/O, a whole
 *   console write): a mutex_t (kernel/sync/mutex.h), which sleeps instead
 *   of masking interrupts.
 * - to wait for a condition guarded by a spinlock, test it with the lock
 *   held and sleep with wait_queue_block_locked(), which releases the lock
 *   only once the process is queued, so a wakeup cannot be lost. re-test
 *   in a loop after waking.
 * - nothing can block before the scheduler is active (the first /bin/sh
 *   runs on the boot stack until it starts a child); see
 *   scheduler_is_active().
 * - never sleep (mutex, wait queue) while holding a spinlock, except
 *   through wait_queue_block_locked() on that same lock. locks don't nest
 *   except heap_lock -> pmm_lock (heap growth).
 * - avoid spin_lock / spin_unlock: spin_unlock enables interrupts
 *   unconditionally, which is wrong whenever the caller had them masked.
 * - lock assertions (KASSERT) are compiled out under NDEBUG; they only fire
 *   in `make debug` builds.
 */

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

#pragma once

/**
 * sleeping mutex for long critical sections in process context (filesystem
 * and disk I/O). a process that finds the mutex held blocks on its wait queue
 * instead of spinning or masking interrupts, so interrupts and other
 * processes keep running while the holder works.
 *
 * never take a mutex from an interrupt handler, and never block on one while
 * holding a spinlock. use a spinlock for short sections shared with IRQs.
 */

#include "common/stdint.h"
#include "kernel/sync/wait_queue.h"

/**
 * a mutex. zero-initialized (or MUTEX_INIT) means unlocked.
 */
typedef struct
{
    volatile bool locked;  // true while held
    struct process *owner; // holder, for assertions and debugging
    wait_queue_t waiters;  // processes blocked in mutex_lock
} mutex_t;

#define MUTEX_INIT {false, 0, {0}}

/**
 * initialize a mutex to the unlocked state.
 * @param mutex the mutex to initialize.
 */
void mutex_init(mutex_t *mutex);

/**
 * acquire a mutex, sleeping until it is free. not recursive.
 * @param mutex the mutex to acquire.
 */
void mutex_lock(mutex_t *mutex);

/**
 * release a mutex held by the current process and wake one waiter.
 * @param mutex the mutex to release.
 */
void mutex_unlock(mutex_t *mutex);

/**
 * check whether the current process holds a mutex (for assertions).
 * @param mutex the mutex to check.
 * @return true if held by the current process.
 */
bool mutex_held(const mutex_t *mutex);

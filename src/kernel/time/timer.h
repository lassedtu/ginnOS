#pragma once

/**
 * kernel timing: the tick count and wait queues with a deadline.
 *
 * the PIT interrupts TIMER_HZ times a second; each interrupt is one tick.
 * a timed queue is a wait queue that a waiter can also leave when a given
 * tick arrives. the timer interrupt wakes every waiter of a queue when the
 * earliest deadline on it has passed; the waiters then check their own
 * deadline and condition, and sleep again if they still have to wait.
 */

#include "common/stdint.h"
#include "drivers/pit/pit.h"
#include "kernel/sync/wait_queue.h"

#define TIMER_HZ PIT_FREQUENCY_HZ

// a deadline that never comes
#define TIMER_NEVER UINT64_MAX

/**
 * a wait queue with a deadline.
 */
typedef struct timed_queue
{
    wait_queue_t waiters;
    volatile uint64_t deadline;   // earliest deadline of a waiter (TIMER_NEVER: none)
    struct timed_queue *next;     // the list timer_tick walks
} timed_queue_t;

/**
 * set up a queue and add it to the ones timer_tick checks. call once per
 * queue (queues live as long as the kernel).
 * @param queue the queue.
 */
void timed_queue_init(timed_queue_t *queue);

/**
 * sleep on a queue until it is woken or the deadline tick has come. the
 * caller must have interrupts masked from its condition check to here, so
 * a wake in between is not lost; it re-checks its condition (and the
 * deadline) after waking.
 * @param queue the queue.
 * @param deadline the tick to wake at, or TIMER_NEVER.
 */
void timed_queue_block(timed_queue_t *queue, uint64_t deadline);

/**
 * like timed_queue_block, but a signal for the process also wakes it.
 */
void timed_queue_block_interruptible(timed_queue_t *queue, uint64_t deadline);

/**
 * wake every waiter of a queue (they re-check their condition).
 * @param queue the queue.
 */
void timed_queue_wake_all(timed_queue_t *queue);

/**
 * called from the timer interrupt on every tick: wake the queues whose
 * earliest deadline has passed.
 * @param now the current tick.
 */
void timer_tick(uint64_t now);

#define TIMER_POLL_HOOKS 6

/** a function the timer interrupt calls on every tick (now: the tick count). */
typedef void (*timer_poll_fn)(uint64_t now);

/**
 * call fn on every tick, in the timer interrupt (interrupts off). for
 * drivers that poll their device, such as the USB keyboard. there is room
 * for TIMER_POLL_HOOKS functions; more are ignored.
 */
void timer_add_poll_hook(timer_poll_fn fn);

/**
 * the current tick.
 */
uint64_t timer_now(void);

/**
 * the number of ticks that covers at least a time span (rounded up).
 * @param ms milliseconds.
 * @return ticks.
 */
uint64_t timer_ms_to_ticks(uint32_t ms);

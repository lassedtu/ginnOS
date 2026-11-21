#pragma once

/**
 * @file completion.h
 * @brief wait for a device: sleep until its interrupt, or poll.
 *
 * a driver starts a command, then calls completion_wait_until with a
 * function that reads the device status. its interrupt handler calls
 * completion_signal. the waiting process sleeps between interrupts, so the
 * CPU runs other processes while the device works.
 *
 * the wait polls instead of sleeping when:
 *   - the driver has no interrupt (use_irq is false),
 *   - the caller cannot sleep (before the scheduler runs, or with
 *     interrupts off),
 *   - the interrupt did not come: if the device is ready after a short
 *     sleep without an interrupt, the interrupt does not work, and the
 *     completion polls from then on (and says so in the kernel log).
 */

#include "common/stdint.h"
#include "kernel/time/timer.h"

typedef struct
{
    volatile bool done;   // set by completion_signal, cleared by the waiter
    bool use_irq;         // the device has a working interrupt
    const char *name;     // for the kernel log
    timed_queue_t queue;  // the sleeping waiter
} completion_t;

/**
 * a function that reads the device status.
 * @return true when the command is done (or has failed).
 */
typedef bool (*completion_ready_t)(void *context);

/**
 * initialize a completion. call once (it registers a timed queue).
 * @param use_irq true if the driver has an interrupt handler.
 */
void completion_init(completion_t *completion, const char *name, bool use_irq);

/**
 * the device has something to report. safe in an interrupt handler.
 */
void completion_signal(completion_t *completion);

/**
 * wait until ready(context) returns true.
 * @param timeout_ms how long the device may take.
 * @return false if the device was not ready in time.
 */
bool completion_wait_until(completion_t *completion, completion_ready_t ready, void *context,
                           uint32_t timeout_ms);

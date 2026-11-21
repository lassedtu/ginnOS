#pragma once

#include "kernel/process/process.h"

/** number of PIT ticks per scheduling time slice (~20ms at 100Hz). */
#define SCHED_TIME_SLICE 2

/**
 * initialize the scheduler.
 * must be called after process_init().
 */
void scheduler_init(void);

/**
 * add a process to the ready queue.
 * @param proc the process to enqueue.
 */
void scheduler_ready(process_t *proc);

/**
 * remove a process from the ready queue.
 * used when a process exits or blocks.
 * @param proc the process to remove.
 */
void scheduler_remove(process_t *proc);

/**
 * called from the PIT IRQ handler on every tick.
 * decrements the time slice counter and triggers a context switch
 * when the slice expires.
 */
void scheduler_tick(void);

/**
 * voluntarily yield the CPU to the next ready process.
 * can be called from kernel code (e.g. when a process blocks).
 */
void scheduler_yield(void);

/**
 * check whether the scheduler has started switching processes. it starts
 * with the first scheduler_ready(); until then (the first /bin/sh runs on
 * the boot stack) scheduler_yield() returns at once, so nothing can block.
 * @return true once processes can be switched.
 */
bool scheduler_is_active(void);

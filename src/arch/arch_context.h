#pragma once

/**
 * arch-neutral kernel context switching.
 *
 * the scheduler drives multitasking through this interface without knowing
 * how a given architecture saves registers or lays out a fresh kernel stack.
 * each architecture supplies its own implementation.
 */

#include "common/stdint.h"

struct process;
typedef struct process process_t;

/**
 * switch kernel execution from one process to another.
 * saves the current callee-saved state on the current kernel stack, writes
 * the resulting stack pointer into *old_sp, then loads new_sp and resumes
 * whatever that stack was set up to run.
 * @param old_sp where to store the outgoing process's kernel stack pointer.
 * @param new_sp the incoming process's saved kernel stack pointer.
 */
void arch_context_switch(uint32_t *old_sp, uint32_t new_sp);

/**
 * prepare a process's kernel stack so its first arch_context_switch() into it
 * begins executing entry_fn. entry_fn takes no arguments and never returns.
 * on return, proc->kernel_esp points at the prepared stack.
 * @param proc the process whose kernel stack to prime (uses kernel_stack).
 * @param entry_fn the function the process should start running.
 */
void arch_setup_initial_stack(process_t *proc, void (*entry_fn)(void));

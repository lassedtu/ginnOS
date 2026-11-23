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
struct registers;

/**
 * switch kernel execution from one process to another.
 * saves the current callee-saved state on the current kernel stack, writes
 * the resulting stack pointer into *old_sp, then loads new_sp and resumes
 * whatever that stack was set up to run.
 * @param old_sp where to store the outgoing process's kernel stack pointer.
 * @param new_sp the incoming process's saved kernel stack pointer.
 */
void arch_context_switch(uintptr_t *old_sp, uintptr_t new_sp);

/**
 * prepare a process's kernel stack so its first arch_context_switch() into it
 * begins executing entry_fn. entry_fn takes no arguments and never returns.
 * on return, proc->kernel_esp points at the prepared stack.
 * @param proc the process whose kernel stack to prime (uses kernel_stack).
 * @param entry_fn the function the process should start running.
 */
void arch_setup_initial_stack(process_t *proc, void (*entry_fn)(void));

/**
 * prepare a forked child's kernel stack so its first arch_context_switch()
 * into it returns to user mode with the parent's registers, except that
 * the syscall returns 0 (and, for a thread, the user stack is its own).
 * @param child the new process (uses kernel_stack, sets kernel_esp).
 * @param regs the parent's saved user registers (its fork syscall frame).
 * @param user_sp the child's user stack pointer, or 0 for the parent's.
 */
void arch_setup_fork_stack(process_t *child, const struct registers *regs, uintptr_t user_sp);

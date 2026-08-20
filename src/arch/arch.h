#pragma once

/**
 * arch-neutral cpu primitives.
 *
 * portable kernel code should include this header instead of
 * referencing architecture-specific headers directly for basic
 * cpu operations like halting, interrupt control, and register access.
 *
 * each architecture provides its own implementation.
 */

#include "common/stdint.h"

/**
 * disable hardware interrupts.
 */
void arch_disable_interrupts(void);

/**
 * enable hardware interrupts.
 */
void arch_enable_interrupts(void);

/**
 * halt the cpu until the next interrupt arrives.
 * interrupts must be enabled before calling this.
 */
void arch_halt(void);

/**
 * disable interrupts and halt permanently.
 * used for fatal error paths where execution must stop.
 */
void arch_halt_forever(void) __attribute__((noreturn));

/**
 * read the current stack pointer.
 */
uint32_t arch_get_stack_pointer(void);

/**
 * switch the active address space (page directory / page table root).
 * @param page_table_phys physical address of the new page table root.
 */
void arch_switch_address_space(uint32_t page_table_phys);

/**
 * drop to usermode (ring 3) and begin executing at entry with the given stack.
 * this function never returns.
 * @param entry userspace entry point address.
 * @param user_esp userspace stack pointer.
 */
void arch_jump_to_usermode(uint32_t entry, uint32_t user_esp) __attribute__((noreturn));

/**
 * reload kernel data segment selectors.
 * used after returning from usermode to restore kernel segment state.
 */
void arch_reload_segments(void);

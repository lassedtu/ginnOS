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
 * disable interrupts and return the prior interrupt state.
 * the return value is opaque and meant only to be handed back to
 * arch_irq_restore(). use this instead of arch_disable_interrupts()
 * when a critical section may nest inside another one.
 * @return the interrupt state as it was before disabling.
 */
uint32_t arch_irq_save(void);

/**
 * restore a previously saved interrupt state.
 * re-enables interrupts only if they were enabled when the matching
 * arch_irq_save() ran, so nested critical sections stay correct.
 * @param flags the value returned by arch_irq_save().
 */
void arch_irq_restore(uint32_t flags);

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
 * an address space handle. opaque to portable code: it identifies a page
 * table root but says nothing about the underlying paging structure, so
 * the same kernel code works whether the arch uses 2-level, 4-level, or
 * some other table layout.
 */
typedef uint32_t addr_space_t;

/**
 * sentinel handle meaning "no address space".
 */
#define ADDR_SPACE_NONE ((addr_space_t)0)

/* page protection flags for arch_map_page(), architecture-neutral. */
#define MMU_FLAG_PRESENT (1u << 0) // the mapping is valid
#define MMU_FLAG_WRITE   (1u << 1) // writable
#define MMU_FLAG_USER    (1u << 2) // reachable from ring 3 / user mode

/* common combination: a writable page owned by a user process. */
#define MMU_USER_RW (MMU_FLAG_PRESENT | MMU_FLAG_WRITE | MMU_FLAG_USER)

/**
 * return the handle for the kernel's own address space.
 * useful when there is no current process to borrow one from.
 */
addr_space_t arch_kernel_address_space(void);

/**
 * create a new address space that shares the kernel mappings.
 * user regions start empty; map them in with arch_map_page().
 * @return the new handle, or ADDR_SPACE_NONE on failure.
 */
addr_space_t arch_create_address_space(void);

/**
 * tear down an address space created with arch_create_address_space(),
 * freeing its user page tables and mapped frames. the kernel address
 * space and ADDR_SPACE_NONE are ignored.
 * @param as the address space to destroy.
 */
void arch_destroy_address_space(addr_space_t as);

/**
 * map one page into a given address space.
 * @param as the target address space.
 * @param virt page-aligned virtual address.
 * @param phys page-aligned physical frame.
 * @param flags MMU_FLAG_* bits describing the mapping.
 * @return 0 on success, -1 on failure.
 */
int arch_map_page(addr_space_t as, uint32_t virt, uint32_t phys, uint32_t flags);

/**
 * translate a virtual address to its physical address within a given
 * address space. lets the kernel reach a process's memory without
 * switching to it (e.g. to stage argv or an ELF image before the swap).
 * @param as the address space to walk.
 * @param virt the virtual address to translate.
 * @return the physical address, or 0 if the page is not mapped.
 */
uint32_t arch_translate_in(addr_space_t as, uint32_t virt);

/**
 * switch the active address space.
 * @param as the address space to make current.
 */
void arch_switch_address_space(addr_space_t as);

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

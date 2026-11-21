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
#include "kernel/memory/phys.h"

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
 * check whether hardware interrupts are currently enabled.
 * meant for assertions such as "must be called with interrupts masked".
 * @return true if interrupts are enabled.
 */
bool arch_irqs_enabled(void);

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
uintptr_t arch_get_stack_pointer(void);

/**
 * walk a chain of saved frame pointers and collect return addresses.
 * stops at a null or misaligned frame, an unmapped frame, a frame that does
 * not move up the stack, or after max entries, so it is safe to call on a
 * corrupt stack from a panic.
 * @param frame the frame pointer to start from (e.g. __builtin_frame_address(0)).
 * @param pcs receives the return addresses, innermost first.
 * @param max capacity of pcs.
 * @return the number of addresses stored.
 */
int arch_stack_walk(uintptr_t frame, uintptr_t *pcs, int max);

/**
 * an address space handle. opaque to portable code: it identifies a page
 * table root but says nothing about the underlying paging structure, so
 * the same kernel code works whether the arch uses 2-level, 4-level, or
 * some other table layout.
 */
typedef uintptr_t addr_space_t;

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
 * copy the user half of an address space into a new, empty one (fork).
 * writable RAM pages become copy-on-write in both: read-only, with the
 * frame shared until one of them writes (mm_fault_in copies it then).
 * @param dst the new address space (from arch_create_address_space()).
 * @param src the address space to copy.
 * @return true on success. on failure dst holds a partial copy; destroy it.
 */
bool arch_copy_user_space(addr_space_t dst, addr_space_t src);

/**
 * tear down an address space created with arch_create_address_space(),
 * freeing its user page tables and mapped frames. the kernel address
 * space and ADDR_SPACE_NONE are ignored.
 * @param as the address space to destroy.
 */
void arch_destroy_address_space(addr_space_t as);

/**
 * set aside a kernel-only virtual range whose mappings every address space
 * shares. call during boot, before the first arch_create_address_space().
 * @param start first virtual address of the range.
 * @param end first virtual address past the range.
 */
void arch_reserve_kernel_range(uintptr_t start, uintptr_t end);

/**
 * map one kernel-only, writable page inside a reserved kernel range. the
 * mapping is visible in every address space at once.
 * @param virt page-aligned virtual address inside a reserved range.
 * @param phys page-aligned physical frame.
 */
void arch_map_kernel_page(uintptr_t virt, phys_addr_t phys);

/**
 * like arch_map_kernel_page(), but the page is not cached: for device
 * registers, where each read and write must go to the device.
 * @param virt page-aligned virtual address inside a reserved range.
 * @param phys page-aligned physical address of the device memory.
 */
void arch_map_device_page(uintptr_t virt, phys_addr_t phys);

/**
 * like arch_map_kernel_page(), but write-combining if the CPU can do it
 * (uncached otherwise): for a framebuffer.
 * @param virt page-aligned virtual address inside a reserved range.
 * @param phys page-aligned physical address of the memory.
 */
void arch_map_wc_page(uintptr_t virt, phys_addr_t phys);

/**
 * remove a page mapped with arch_map_kernel_page(). the frame is not freed.
 * @param virt page-aligned virtual address.
 */
void arch_unmap_kernel_page(uintptr_t virt);

/**
 * check that user code in an address space could access a whole range:
 * every page present and user-accessible, and writable if write is set.
 * @param as the address space to check.
 * @param addr first byte of the range.
 * @param len length in bytes.
 * @param write true if the range will be written.
 * @return true if every page allows the access.
 */
bool arch_user_access_ok(addr_space_t as, uintptr_t addr, size_t len, bool write);

/**
 * map one page into a given address space.
 * @param as the target address space.
 * @param virt page-aligned virtual address.
 * @param phys page-aligned physical frame.
 * @param flags MMU_FLAG_* bits describing the mapping.
 * @return 0 on success, -1 on failure.
 */
int arch_map_page(addr_space_t as, uintptr_t virt, phys_addr_t phys, uint32_t flags);

/**
 * remove a user page mapping (and its TLB entry). the frame is not freed.
 * @param as the address space.
 * @param virt page-aligned user address.
 * @return the frame that was mapped there, or 0 if the page was not mapped.
 */
phys_addr_t arch_unmap_page(addr_space_t as, uintptr_t virt);

/**
 * change the protection of a mapped user page, keeping its frame.
 * @param as the address space.
 * @param virt page-aligned user address.
 * @param flags MMU_FLAG_* bits; without MMU_FLAG_USER the page stays mapped
 *        but user code can no longer touch it.
 * @return true if the page was mapped and changed.
 */
bool arch_protect_page(addr_space_t as, uintptr_t virt, uint32_t flags);

/**
 * translate a virtual address to its physical address within a given
 * address space. lets the kernel reach a process's memory without
 * switching to it (e.g. to stage argv or an ELF image before the swap).
 * @param as the address space to walk.
 * @param virt the virtual address to translate.
 * @return the physical address, or 0 if the page is not mapped.
 */
phys_addr_t arch_translate_in(addr_space_t as, uintptr_t virt);

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
void arch_jump_to_usermode(uintptr_t entry, uintptr_t user_esp) __attribute__((noreturn));

// bytes of saved FPU/SSE state per process (16-byte aligned)
#define ARCH_FPU_STATE_SIZE 512

/**
 * turn on the FPU (and SSE) for user programs and make the clean state a
 * new program starts with. call once at boot.
 */
void arch_fpu_init(void);

/**
 * save the CPU's FPU/SSE registers.
 * @param area ARCH_FPU_STATE_SIZE bytes, 16-byte aligned.
 */
void arch_fpu_save(void *area);

/**
 * load the FPU/SSE registers.
 * @param area state saved by arch_fpu_save (or arch_fpu_clean).
 */
void arch_fpu_restore(const void *area);

/**
 * fill an area with the state of a fresh program.
 */
void arch_fpu_clean(void *area);

/**
 * make a state that came from user memory safe to load (a signal frame).
 */
void arch_fpu_sanitize(void *area);

/**
 * reload kernel data segment selectors.
 * used after returning from usermode to restore kernel segment state.
 */
void arch_reload_segments(void);

#pragma once

/**
 * kernel stacks with guard pages.
 *
 * every process's kernel stack lives in a fixed virtual region
 * (KSTACK_REGION_BASE) split into one slot per process. a slot is an unmapped
 * guard page followed by KERNEL_STACK_SIZE bytes of mapped stack, so running
 * off the bottom of a stack faults on the guard page instead of silently
 * overwriting whatever physical frame lies below it. the region's page table
 * is reserved at boot and shared by every address space, so a stack mapped
 * later is reachable from any process.
 */

#include "common/stdint.h"
#include "kernel/memory/page.h"

// size of the unmapped guard page below each stack
#define KSTACK_GUARD_SIZE PAGE_SIZE

/**
 * reserve the kernel stack region. must run after paging_init() and before
 * the first address space is created (process_create()).
 */
void kstack_init(void);

/**
 * allocate and map a zeroed kernel stack.
 * @return the lowest address of the stack (it grows down from
 *         base + KERNEL_STACK_SIZE), or 0 if no slot or memory is free.
 */
uintptr_t kstack_alloc(void);

/**
 * unmap a kernel stack and return its frames and slot. never free the stack
 * the CPU is currently running on.
 * @param base the value kstack_alloc() returned.
 */
void kstack_free(uintptr_t base);

/**
 * check whether an address falls in the guard page of any stack slot.
 * @param addr the address to check (e.g. a page fault's CR2).
 * @return true if addr is inside a guard page.
 */
bool kstack_is_guard(uintptr_t addr);

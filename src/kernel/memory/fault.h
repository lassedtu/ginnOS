#pragma once

/**
 * user page faults: demand paging and killing processes that touch memory
 * they have not mapped.
 *
 * user memory is only partly backed up front: the heap (sbrk), anonymous
 * mmaps and the stack below its first pages get a physical frame the first
 * time they are touched. the page-fault handler (and the kernel's user-copy
 * helpers, before they touch user memory) ask mm_fault_in to back a page; an
 * access no area allows kills the process instead of the kernel.
 */

#include "common/stdint.h"
#include "kernel/process/process.h"

/**
 * back the page containing addr if the process's areas allow the access:
 * allocate a zeroed frame and map it with the area's protection.
 * @param proc the process (its own address space is the target).
 * @param addr the user address being accessed.
 * @param write true for a write access.
 * @return true if the page is now mapped for that access; false if no area
 *         allows it (or memory ran out).
 */
bool mm_fault_in(process_t *proc, uintptr_t addr, bool write);

/**
 * handle a page fault raised by user code. returns if the page was backed
 * and the instruction can be retried; otherwise prints a segmentation fault
 * report and terminates the process (exit status 139, 128 + SIGSEGV).
 * @param addr the faulting address (CR2).
 * @param write true if the access was a write.
 * @param present true if the page was present (a protection violation).
 * @param pc the faulting instruction.
 */
void mm_handle_user_fault(uintptr_t addr, bool write, bool present, uintptr_t pc);

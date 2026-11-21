#pragma once

/**
 * double-fault handling through a task gate.
 *
 * a kernel stack overflow faults on the stack's guard page, and the CPU then
 * fails to push the page-fault frame onto that same stack: a double fault.
 * an ordinary interrupt gate would push onto the dead stack again and triple
 * fault (a silent reset), so vector 8 is a task gate instead. the CPU
 * switches to a separate TSS with its own stack, and the handler reports a
 * "kernel stack overflow" panic with the overflowing process and a backtrace.
 */

/**
 * install the double-fault task and point IDT vector 8 at it. call after
 * gdt_initialize() and exception_initialize().
 */
void double_fault_initialize(void);

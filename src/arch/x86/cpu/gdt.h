#pragma once

#include "common/stdint.h"

/** kernel code segment selector (GDT index 1, RPL 0). */
#define GDT_KERNEL_CODE 0x08

/** kernel data segment selector (GDT index 2, RPL 0). */
#define GDT_KERNEL_DATA 0x10

/** user code segment selector (GDT index 3, RPL 3). */
#define GDT_USER_CODE 0x1B

/** user data segment selector (GDT index 4, RPL 3). */
#define GDT_USER_DATA 0x23

/** TSS segment selector (GDT index 5, RPL 0). */
#define GDT_TSS 0x28

/** double-fault task TSS selector (GDT index 6, RPL 0). */
#define GDT_DOUBLE_FAULT_TSS 0x30

/**
 * initialize the Global Descriptor Table with kernel segments, user segments,
 * and a Task State Segment. Loads the GDT into GDTR and the TSS into TR.
 */
void gdt_initialize(void);

/**
 * update the kernel stack pointer (ESP0) in the TSS.
 * called on context switches so the CPU knows where to find the kernel stack
 * when an interrupt occurs in ring 3.
 * @param esp0 the kernel stack pointer to store.
 */
void tss_set_kernel_stack(uintptr_t esp0);

/**
 * set up the double-fault task: a second TSS whose state the CPU loads when
 * a double fault arrives through a task gate, so the handler runs on its own
 * stack even when the faulting stack is unusable (a kernel stack overflow
 * into its guard page).
 * @param entry function the task starts in; it must never return.
 * @param stack_top initial ESP for the task.
 */
void tss_install_double_fault(void (*entry)(void), uintptr_t stack_top);

/**
 * set the page directory the double-fault task runs with.
 * @param cr3 physical address of the kernel page directory.
 */
void tss_set_double_fault_cr3(uint32_t cr3);

/**
 * read the state of the task that was running when the double fault struck.
 * the task switch saves it into the main TSS.
 * @param eip receives the interrupted EIP.
 * @param esp receives the interrupted ESP.
 * @param ebp receives the interrupted EBP.
 */
void tss_interrupted_state(uint32_t *eip, uint32_t *esp, uint32_t *ebp);

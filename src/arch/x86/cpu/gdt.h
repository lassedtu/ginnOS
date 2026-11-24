#pragma once

#include "common/stdint.h"

#ifdef __x86_64__

/*
 * the x86_64 GDT. the user data segment comes before the user code segment
 * because SYSRET loads them as STAR[63:48] + 8 and + 16.
 */

/** kernel code segment selector (GDT index 1, RPL 0, 64-bit). */
#define GDT_KERNEL_CODE 0x08

/** kernel data segment selector (GDT index 2, RPL 0). */
#define GDT_KERNEL_DATA 0x10

/** user data segment selector (GDT index 3, RPL 3). */
#define GDT_USER_DATA 0x1B

/** user code segment selector (GDT index 4, RPL 3, 64-bit). */
#define GDT_USER_CODE 0x23

/** TSS segment selector (GDT indexes 5 and 6: a 16-byte descriptor). */
#define GDT_TSS 0x28

#else

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

// user thread-local storage, loaded in GS (entry 7, RPL 3)
#define GDT_USER_TLS 0x3B

#endif

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

#ifndef __x86_64__
/**
 * set the base of the user TLS segment and load GS with it.
 * @param base the thread pointer.
 */
void gdt_set_tls(uint32_t base);
#endif

#ifdef __x86_64__

/**
 * set an interrupt stack table entry of the TSS. an IDT gate with this IST
 * index (1 to 7) switches to the stack, also in kernel mode.
 * @param index the IST index, 1 to 7.
 * @param stack_top the top of the stack.
 */
void tss_set_ist(int index, uintptr_t stack_top);

/* the kernel stack pointer for the syscall entry (asm64/syscall.asm). it
 * changes with the TSS RSP0. */
extern uintptr_t syscall_kernel_rsp;

#else

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

#endif // __x86_64__

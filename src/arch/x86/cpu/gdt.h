#ifndef GDT_H
#define GDT_H

#include "../../../common/stdint.h"

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
void tss_set_kernel_stack(uint32_t esp0);

#endif

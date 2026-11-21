#include "arch/arch_context.h"

#include "kernel/process/process.h"
#include "arch/x86/cpu/isr.h"

// the return half of isr_common (isr.asm): pops a struct registers, then irets
extern void isr_exit(void);

/**
 * @file arch_context_x86.c
 * @brief x86 implementation of the arch-neutral context-switch helpers.
 *
 * arch_context_switch itself is pure assembly (context_switch.asm). the one
 * piece that needs C is priming a brand-new kernel stack so that the first
 * switch into a process "returns" into its entry function. that layout mirrors
 * exactly what arch_context_switch pushes, so it lives next to the asm here.
 */

void arch_setup_initial_stack(process_t *proc, void (*entry_fn)(void))
{
    // build the stack top-down. arch_context_switch returns by popping the
    // four callee-saved registers and then executing 'ret', so we lay down a
    // return address followed by four zeroed register slots.
    uintptr_t *sp = (uintptr_t *)(proc->kernel_stack + KERNEL_STACK_SIZE);

    *(--sp) = (uintptr_t)entry_fn; // eip: where 'ret' lands
    *(--sp) = 0;                  // ebp
    *(--sp) = 0;                  // ebx
    *(--sp) = 0;                  // esi
    *(--sp) = 0;                  // edi  <- kernel_esp points here

    proc->kernel_esp = (uintptr_t)sp;
}

void arch_setup_fork_stack(process_t *child, const struct registers *regs)
{
    // the saved user registers go at the top of the stack, where the CPU and
    // isr_common put them on a syscall from user mode, so the TSS's esp0
    // and this frame agree
    uintptr_t top = child->kernel_stack + KERNEL_STACK_SIZE;
    struct registers *frame = (struct registers *)(top - sizeof(struct registers));
    *frame = *regs;
    REG_RESULT(frame) = 0; // fork returns 0 in the child

    // below it, the frame arch_context_switch pops: four registers, then a
    // return address into isr_exit, with ESP left at the saved registers
    uintptr_t *sp = (uintptr_t *)frame;
    *(--sp) = (uintptr_t)isr_exit; // eip
    *(--sp) = 0;                   // ebp
    *(--sp) = 0;                   // ebx
    *(--sp) = 0;                   // esi
    *(--sp) = 0;                   // edi  <- kernel_esp points here
    child->kernel_esp = (uintptr_t)sp;
}

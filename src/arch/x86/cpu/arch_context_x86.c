#include "arch/arch_context.h"

#include "kernel/process/process.h"

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
    uint32_t *sp = (uint32_t *)(proc->kernel_stack + KERNEL_STACK_SIZE);

    *(--sp) = (uint32_t)entry_fn; // eip: where 'ret' lands
    *(--sp) = 0;                  // ebp
    *(--sp) = 0;                  // ebx
    *(--sp) = 0;                  // esi
    *(--sp) = 0;                  // edi  <- kernel_esp points here

    proc->kernel_esp = (uint32_t)sp;
}

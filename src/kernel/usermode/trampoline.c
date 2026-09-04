#include "usermode.h"

#include "arch/arch.h"
#include "kernel/process/process.h"
#include "kernel/panic.h"
#include "common/stdint.h"

/**
 * trampoline function for child processes launched via the scheduler.
 * the child's kernel stack is set up so that context_switch returns here.
 * reads the entry point from process_current()->entry.
 * this function never returns.
 */
static void process_entry_trampoline(void)
{
    process_t *proc = process_current();
    if (!proc)
    {
        kernel_panic("trampoline: no current process");
    }

    // switch to this process's address space
    arch_switch_address_space(proc->page_directory);

    uint32_t entry = proc->entry;
    const char **argv = (const char **)proc->argv;

    jump_to_usermode(entry, proc->page_directory, argv);
}

/**
 * set up a child process's kernel stack so that when context_switch
 * switches to it, it "returns" into process_entry_trampoline.
 *
 * stack layout (growing downward):
 *   &trampoline        <- context_switch ret lands here
 *   0 (ebp)            <- context_switch pops these
 *   0 (ebx)
 *   0 (esi)
 *   0 (edi)            <- child's kernel_esp points here
 */
void setup_child_stack(process_t *child, uint32_t entry)
{
    uint32_t *sp = (uint32_t *)(child->kernel_stack + KERNEL_STACK_SIZE);

    // return address for context_switch's ret instruction
    *(--sp) = (uint32_t)process_entry_trampoline;

    // fake callee-saved registers (context_switch pops: edi, esi, ebx, ebp)
    *(--sp) = 0; // ebp
    *(--sp) = 0; // ebx
    *(--sp) = 0; // esi
    *(--sp) = 0; // edi

    child->kernel_esp = (uint32_t)sp;
    child->entry = entry;
}

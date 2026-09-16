#include "usermode.h"

#include "arch/arch.h"
#include "arch/arch_context.h"
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
 * set up a child process's kernel stack so that when arch_context_switch
 * switches to it, it "returns" into process_entry_trampoline. the ELF entry
 * point is stashed in the process so the trampoline can read it later.
 */
void setup_child_stack(process_t *child, uint32_t entry)
{
    child->entry = entry;
    arch_setup_initial_stack(child, process_entry_trampoline);
}

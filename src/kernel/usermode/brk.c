#include "usermode.h"

#include "kernel/process/process.h"

uintptr_t usermode_get_brk(void)
{
    process_t *proc = process_current();
    if (!proc)
    {
        return 0;
    }
    return proc->brk;
}

void usermode_set_brk(uintptr_t brk)
{
    process_t *proc = process_current();
    if (proc)
    {
        proc->brk = brk;
    }
}

#include "ktest.h"

#include "kernel/memory/pmm.h"
#include "kernel/process/process.h"

KTEST(process_create_destroy_slot)
{
    uint32_t free_before = pmm_free_count();

    process_t *a = process_create();
    process_t *b = process_create();
    KEXPECT(a != NULL && b != NULL);
    if (!a || !b)
        return;

    // distinct slots and increasing pids; not runnable until scheduled
    KEXPECT(a != b);
    KEXPECT(b->pid > a->pid);
    KEXPECT_EQ(a->state, PROC_STATE_NEW);
    KEXPECT(process_get(a->pid) == a);
    KEXPECT(process_get(b->pid) == b);

    uint32_t a_pid = a->pid;
    process_destroy(a);
    KEXPECT(process_get(a_pid) == NULL);
    KEXPECT_EQ(a->state, PROC_STATE_UNUSED);

    // the freed slot is reused with a new pid, never the old one
    process_t *c = process_create();
    KEXPECT(c != NULL);
    if (c)
    {
        KEXPECT(c->pid > b->pid);
        process_destroy(c);
    }
    process_destroy(b);

    // kernel stacks and address spaces went back to the PMM
    KEXPECT_EQ(pmm_free_count(), free_before);
}

KTEST(process_table_full_returns_null)
{
    process_t *procs[PROCESS_MAX];
    uint32_t created = 0;

    while (created < PROCESS_MAX)
    {
        procs[created] = process_create();
        if (!procs[created])
            break;
        created++;
    }

    // the test kernel runs no processes, so every slot is available
    KEXPECT_EQ(created, PROCESS_MAX);
    KEXPECT(process_create() == NULL);

    for (uint32_t i = 0; i < created; i++)
        process_destroy(procs[i]);

    // every slot came back
    process_t *again = process_create();
    KEXPECT(again != NULL);
    if (again)
        process_destroy(again);
}

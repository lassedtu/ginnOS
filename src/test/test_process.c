#include "ktest.h"

#include "arch/arch.h"
#include "kernel/memory/heap.h"
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

KTEST(process_orphan_children_detaches_and_destroys)
{
    uint32_t free_before = pmm_free_count();

    process_t *parent = process_create();
    process_t *running = process_create();
    process_t *zombie = process_create();
    KEXPECT(parent && running && zombie);
    if (!parent || !running || !zombie)
        return;

    running->parent_pid = parent->pid;
    zombie->parent_pid = parent->pid;

    // the zombie child exited: it released its resources, nobody reaped it
    process_release_resources(zombie);
    zombie->state = PROC_STATE_ZOMBIE;
    uint32_t zombie_pid = zombie->pid;

    process_orphan_children(parent);

    // the zombie child is gone, the running one has lost its parent
    KEXPECT(process_get(zombie_pid) == NULL);
    KEXPECT_EQ(running->parent_pid, PID_NONE);

    // the orphan exits: the kernel reaps it after the next context switch
    process_release_resources(running);
    uint32_t running_pid = running->pid;
    uint32_t flags = arch_irq_save();
    running->state = PROC_STATE_ZOMBIE;
    process_note_orphan_zombie();
    process_reap_orphans();
    arch_irq_restore(flags);
    KEXPECT(process_get(running_pid) == NULL);

    process_destroy(parent);
    KEXPECT_EQ(pmm_free_count(), free_before);
}

KTEST(process_destroy_frees_argv)
{
    heap_stats_t before = heap_get_stats();

    process_t *proc = process_create();
    KEXPECT(proc != NULL);
    if (!proc)
        return;

    // the shape exec_program gives argv: heap strings in a heap array
    proc->argv = kmalloc(3 * sizeof(char *));
    KEXPECT(proc->argv != NULL);
    if (proc->argv)
    {
        proc->argv[0] = kmalloc(8);
        proc->argv[1] = kmalloc(16);
        proc->argv[2] = NULL;
    }

    process_destroy(proc);
    KEXPECT_EQ(heap_get_stats().current_usage, before.current_usage);
}

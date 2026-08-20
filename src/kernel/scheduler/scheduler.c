#include "scheduler.h"
#include "kernel/process/process.h"
#include "arch/x86/cpu/gdt.h"
#include "arch/x86/cpu/paging.h"
#include "common/memory.h"

/**
 * context_switch defined in context_switch.asm.
 * saves current state on old stack, switches to new stack.
 */
extern void context_switch(uint32_t *old_esp, uint32_t new_esp);

// simple circular ready queue (ring buffer of process pointers).
#define READY_QUEUE_SIZE PROCESS_MAX

static process_t *ready_queue[READY_QUEUE_SIZE];
static int queue_head = 0; // next slot to dequeue from
static int queue_tail = 0; // next slot to enqueue into
static int queue_count = 0;

// remaining ticks before forcing a context switch.
static uint32_t ticks_remaining = SCHED_TIME_SLICE;

// flag: scheduler is active (set after first process is scheduled).
static int scheduler_active = 0;

/**
 * enqueue a process into the ready queue.
 */
static void queue_push(process_t *proc)
{
    if (queue_count >= READY_QUEUE_SIZE)
    {
        return; // queue full drop silently
    }

    ready_queue[queue_tail] = proc;
    queue_tail = (queue_tail + 1) % READY_QUEUE_SIZE;
    queue_count++;
}

/**
 * dequeue the next process from the ready queue.
 * @return process pointer, or NULL if queue is empty.
 */
static process_t *queue_pop(void)
{
    if (queue_count == 0)
    {
        return (void *)0;
    }

    process_t *proc = ready_queue[queue_head];
    queue_head = (queue_head + 1) % READY_QUEUE_SIZE;
    queue_count--;
    return proc;
}

void scheduler_init(void)
{
    memset(ready_queue, 0, sizeof(ready_queue));
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
    ticks_remaining = SCHED_TIME_SLICE;
    scheduler_active = 0;
}

void scheduler_ready(process_t *proc)
{
    if (!proc)
    {
        return;
    }

    proc->state = PROC_STATE_READY;
    queue_push(proc);

    if (!scheduler_active)
    {
        scheduler_active = 1;
    }
}

void scheduler_remove(process_t *proc)
{
    if (!proc)
    {
        return;
    }

    // scan the queue and remove the process
    int new_count = 0;
    int read = queue_head;

    for (int i = 0; i < queue_count; i++)
    {
        int idx = (read + i) % READY_QUEUE_SIZE;
        if (ready_queue[idx] != proc)
        {
            ready_queue[new_count] = ready_queue[idx];
            new_count++;
        }
    }

    // compact the queue
    queue_head = 0;
    queue_tail = new_count;
    queue_count = new_count;
}

/**
 * perform the actual context switch to the next ready process.
 */
static void schedule(void)
{
    process_t *current = process_current();
    process_t *next = queue_pop();

    if (!next)
    {
        // no other process ready continue running current
        ticks_remaining = SCHED_TIME_SLICE;
        return;
    }

    if (current && current->state == PROC_STATE_RUNNING)
    {
        // current process is still runnable put it back in the queue
        current->state = PROC_STATE_READY;
        queue_push(current);
    }

    // switch to the next process
    next->state = PROC_STATE_RUNNING;
    process_set_current(next);

    // update TSS kernel stack to the new process's kernel stack top
    tss_set_kernel_stack(next->kernel_stack + KERNEL_STACK_SIZE);

    // switch to the new process's page directory
    if (next->page_directory)
    {
        paging_switch_directory(next->page_directory);
    }

    // reset time slice
    ticks_remaining = SCHED_TIME_SLICE;

    // perform the context switch
    if (current)
    {
        context_switch(&current->kernel_esp, next->kernel_esp);
    }
    else
    {
        // no previous context to save (first schedule call)
        // just load the new context by switching from a dummy
        uint32_t dummy_esp;
        context_switch(&dummy_esp, next->kernel_esp);
    }
}

void scheduler_tick(void)
{
    if (!scheduler_active)
    {
        return;
    }

    if (ticks_remaining > 0)
    {
        ticks_remaining--;
    }

    if (ticks_remaining == 0)
    {
        schedule();
    }
}

void scheduler_yield(void)
{
    if (!scheduler_active)
    {
        return;
    }

    ticks_remaining = 0;
    schedule();
}

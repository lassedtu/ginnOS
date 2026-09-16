#include "scheduler.h"
#include "kernel/process/process.h"
#include "arch/arch.h"
#include "arch/arch_context.h"
#include "arch/x86/cpu/gdt.h"
#include "common/memory.h"

// doubly-linked intrusive ready queue (head and tail).
static process_t *queue_head;
static process_t *queue_tail;
static uint32_t queue_count;

// remaining ticks before forcing a context switch.
static uint32_t ticks_remaining;

// flag: scheduler is active (set after first process is scheduled).
static int scheduler_active;

// idle process: runs when no other process is ready.
static process_t idle_proc;
static uint8_t idle_stack[KERNEL_STACK_SIZE] __attribute__((aligned(4)));

/**
 * idle loop: halts the CPU until the next interrupt.
 * runs as a regular scheduled process with the lowest implicit priority
 * (it's only selected when the ready queue is empty).
 */
static void idle_entry(void)
{
    for (;;)
    {
        arch_enable_interrupts();
        arch_halt();
    }
}

/**
 * append a process to the tail of the ready queue.
 */
static void queue_push(process_t *proc)
{
    proc->ready_next = NULL;
    proc->ready_prev = queue_tail;

    if (queue_tail)
    {
        queue_tail->ready_next = proc;
    }
    else
    {
        queue_head = proc;
    }

    queue_tail = proc;
    queue_count++;
}

/**
 * remove and return the process at the head of the ready queue.
 * @return process pointer, or NULL if queue is empty.
 */
static process_t *queue_pop(void)
{
    if (!queue_head)
    {
        return NULL;
    }

    process_t *proc = queue_head;
    queue_head = proc->ready_next;

    if (queue_head)
    {
        queue_head->ready_prev = NULL;
    }
    else
    {
        queue_tail = NULL;
    }

    proc->ready_next = NULL;
    proc->ready_prev = NULL;
    queue_count--;
    return proc;
}

/**
 * remove a specific process from anywhere in the ready queue.
 * O(1) since we have prev/next pointers.
 */
static void queue_remove(process_t *proc)
{
    if (proc->ready_prev)
    {
        proc->ready_prev->ready_next = proc->ready_next;
    }
    else
    {
        queue_head = proc->ready_next;
    }

    if (proc->ready_next)
    {
        proc->ready_next->ready_prev = proc->ready_prev;
    }
    else
    {
        queue_tail = proc->ready_prev;
    }

    proc->ready_next = NULL;
    proc->ready_prev = NULL;
    queue_count--;
}

/**
 * check whether a process is currently in the ready queue.
 */
static bool queue_contains(process_t *proc)
{
    return proc == queue_head || proc->ready_prev != NULL || proc->ready_next != NULL;
}

void scheduler_init(void)
{
    queue_head = NULL;
    queue_tail = NULL;
    queue_count = 0;
    ticks_remaining = SCHED_TIME_SLICE;
    scheduler_active = 0;

    // set up the idle process (never goes through process_create)
    memset(&idle_proc, 0, sizeof(idle_proc));
    idle_proc.pid = 0;
    idle_proc.state = PROC_STATE_READY;
    idle_proc.kernel_stack = (uint32_t)idle_stack;
    idle_proc.kernel_esp = (uint32_t)idle_stack + KERNEL_STACK_SIZE;

    // prime the idle stack so the first switch into it runs idle_entry.
    arch_setup_initial_stack(&idle_proc, idle_entry);
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
    if (!proc || !queue_contains(proc))
    {
        return;
    }

    queue_remove(proc);
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
        if (current && current != &idle_proc && current->state == PROC_STATE_RUNNING)
        {
            // current is the only runnable process, keep it running
            ticks_remaining = SCHED_TIME_SLICE;
            return;
        }

        // nothing runnable, switch to idle
        next = &idle_proc;
    }

    if (current && current != &idle_proc && current->state == PROC_STATE_RUNNING)
    {
        // current process is still runnable, put it back in the queue
        current->state = PROC_STATE_READY;
        queue_push(current);
    }

    if (next == current)
    {
        // popped ourselves, no actual switch needed
        next->state = PROC_STATE_RUNNING;
        ticks_remaining = SCHED_TIME_SLICE;
        return;
    }

    // switch to the next process
    next->state = PROC_STATE_RUNNING;
    process_set_current(next);

    // update TSS kernel stack to the new process's kernel stack top
    tss_set_kernel_stack(next->kernel_stack + KERNEL_STACK_SIZE);

    // switch to the new process's address space
    if (next->page_directory)
    {
        arch_switch_address_space(next->page_directory);
    }

    // reset time slice
    ticks_remaining = SCHED_TIME_SLICE;

    // perform the context switch
    if (current)
    {
        arch_context_switch(&current->kernel_esp, next->kernel_esp);
    }
    else
    {
        // no previous context to save (first schedule call)
        uint32_t dummy_esp;
        arch_context_switch(&dummy_esp, next->kernel_esp);
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

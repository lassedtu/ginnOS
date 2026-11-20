#pragma once

#include "common/stdint.h"
#include "kernel/memory/page.h"
#include "arch/arch.h"
#include "kernel/syscall/fd_table.h"

// maximum number of concurrent processes.
#define PROCESS_MAX 64

// invalid PID sentinel.
#define PID_NONE 0

// the first process (/bin/sh for now); the system halts if it exits.
#define PID_INIT 1

// size of per-process kernel stack (4 KiB).
#define KERNEL_STACK_SIZE (2 * PAGE_SIZE)

/**
 * process states.
 */
typedef enum
{
    PROC_STATE_UNUSED = 0, // slot is free
    PROC_STATE_NEW,        // slot claimed by process_create, not yet runnable
    PROC_STATE_RUNNING,    // currently executing
    PROC_STATE_READY,      // runnable, waiting for CPU
    PROC_STATE_BLOCKED,    // waiting for I/O or event
    PROC_STATE_ZOMBIE,     // exited, waiting to be reaped
} process_state_t;

#define PATH_MAX 256

/**
 * process control block.
 */
typedef struct process
{
    uint32_t pid;           // process identifier
    uint32_t parent_pid;    // parent PID; PID_NONE for PID 1 and for orphans,
                            // which the kernel reaps itself when they exit
    process_state_t state;  // current state
    uintptr_t brk;          // program break (for sbrk)
    uintptr_t mmap_next;    // next free virtual address in the mmap region (bump allocator)
    int32_t exit_code;      // exit code (valid in ZOMBIE state)
    uintptr_t kernel_stack; // lowest address of the kernel stack (kstack.h)
    uintptr_t kernel_esp;   // saved kernel ESP (for context switch)
    uintptr_t entry;        // ELF entry point (used by trampoline on first schedule)
    addr_space_t page_directory; // this process's address space (page directory)
    uint32_t wait_for_pid;  // PID this process is waiting for (0 = not waiting)
    char **argv;            // kernel-heap copy of argv (freed at exit)
    uint8_t tty_raw;        // 0 = cooked (line-buffered), 1 = raw (event-based)

    // intrusive doubly-linked list for the scheduler ready queue.
    struct process *ready_next;
    struct process *ready_prev;

    // intrusive singly-linked list for a wait queue (Phase L3).
    // valid only while state == PROC_STATE_BLOCKED and the process sits
    // on some wait_queue_t. separate from the ready links so a process is
    // never on both lists at once.
    struct process *wait_next;

    char cwd[PATH_MAX];     // current working directory
    fd_entry_t fds[FD_MAX]; // per-process file descriptor table
} process_t;

/**
 * initialize the process subsystem.
 * must be called once during kernel startup.
 */
void process_init(void);

/**
 * create a new process and return its PCB.
 * allocates a PID and initializes the fd table with stdin/stdout/stderr.
 * the process starts in PROC_STATE_NEW; scheduler_ready() makes it runnable.
 * @return pointer to the new process, or NULL if the table is full or its
 *         kernel stack or address space could not be allocated.
 */
process_t *process_create(void);

/**
 * get the currently running process.
 * @return pointer to the current process, or NULL if none is running.
 */
process_t *process_current(void);

/**
 * set the currently running process.
 * @param proc the process to mark as current.
 */
void process_set_current(process_t *proc);

/**
 * destroy a process and free its slot in the process table.
 * closes any open file descriptors.
 * @param proc the process to destroy.
 */
void process_destroy(process_t *proc);

/**
 * release what a process owns apart from its kernel stack and table slot:
 * close its fds, free its argv copy and destroy its address space (after
 * switching away from it, when it is the current process). run by an
 * exiting process in its own context, since closing a file may sleep.
 * safe to call more than once.
 * @param proc the process.
 */
void process_release_resources(process_t *proc);

/**
 * detach the children of an exiting process: zombie children are destroyed
 * now, running ones lose their parent (PID_NONE) and are reaped by the
 * kernel when they exit.
 * @param parent the exiting process.
 */
void process_orphan_children(process_t *parent);

/**
 * record that an orphan (no parent) has become a zombie and must be reaped
 * by the kernel. call with interrupts masked, just before it switches away.
 */
void process_note_orphan_zombie(void);

/**
 * destroy orphan zombies. called by the scheduler right after a context
 * switch, on the next process's stack, so a dead process's own stack is
 * never freed while in use. runs with interrupts masked.
 */
void process_reap_orphans(void);

/**
 * look up a process by PID. the pointer stays valid only while the process
 * cannot be destroyed (it is the caller, or the caller's unreaped child).
 * @param pid the process identifier.
 * @return pointer to the process, or NULL if not found.
 */
process_t *process_get(uint32_t pid);

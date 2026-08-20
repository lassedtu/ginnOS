#pragma once

#include "common/stdint.h"
#include "kernel/syscall/fd_table.h"

// maximum number of concurrent processes.
#define PROCESS_MAX 64

// invalid PID sentinel.
#define PID_NONE 0

// size of per-process kernel stack (4 KiB).
#define KERNEL_STACK_SIZE 4096

/**
 * process states.
 */
typedef enum
{
    PROC_STATE_UNUSED = 0, // slot is free
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
    uint32_t parent_pid;    // parent process PID (PID_NONE if orphan)
    process_state_t state;  // current state
    uint32_t brk;           // program break (for sbrk)
    int32_t exit_code;      // exit code (valid in ZOMBIE state)
    uint32_t kernel_stack;  // base address of kernel stack page
    uint32_t kernel_esp;    // saved kernel ESP (for context switch)
    uint32_t entry;         // ELF entry point (used by trampoline on first schedule)
    uint32_t page_directory; // physical address of this process's page directory
    uint32_t wait_for_pid;  // PID this process is waiting for (0 = not waiting)
    char **argv;            // kernel-heap copy of argv (freed after first schedule)
    uint8_t tty_raw;        // 0 = cooked (line-buffered), 1 = raw (event-based)
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
 * @return pointer to the new process, or NULL if the table is full.
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
 * look up a process by PID.
 * @param pid the process identifier.
 * @return pointer to the process, or NULL if not found.
 */
process_t *process_get(uint32_t pid);

#pragma once

#include "common/stdint.h"
#include "kernel/memory/page.h"
#include "kernel/memory/vma.h"
#include "kernel/memory/user_space.h"
#include "arch/arch.h"
#include "kernel/syscall/fd_table.h"
#include "kernel/sync/wait_queue.h"
#include "kernel/signal/signal.h"

// maximum number of concurrent processes.
#define PROCESS_MAX 64

// invalid PID sentinel.
#define PID_NONE 0

// the first process (/bin/init); the system halts if it exits.
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
    PROC_STATE_STOPPED,    // stopped by a signal until SIGCONT
} process_state_t;

/**
 * a change of a child that waitpid has not reported yet.
 */
typedef enum
{
    WAIT_REPORT_NONE = 0,
    WAIT_REPORT_STOPPED,   // stopped (WUNTRACED)
    WAIT_REPORT_CONTINUED, // continued (WCONTINUED)
} wait_report_t;

#define PATH_MAX 256

// longest process name, NUL included
#define PROCESS_NAME_MAX 32

/**
 * where paths start, and the mode bits new files lose. the threads of a
 * process use one together; it is freed when the last lets it go.
 */
typedef struct fs_context
{
    uint32_t users;      // the processes (threads) that use it
    uint32_t umask;      // permission bits taken off new files
    char cwd[PATH_MAX];  // current working directory (the real path)
    char root[PATH_MAX]; // the root directory (chroot): a path starts here
} fs_context_t;

/**
 * process control block.
 */
typedef struct process
{
    uint32_t pid;           // process identifier (the thread id)
    uint32_t tgid;          // the thread group: the pid of its first thread
                            // (the leader). getpid gives it.
    struct process *leader; // the leader (itself for it); its slot stays
                            // until every thread of the group has ended
    uint32_t threads;       // in the leader: the threads not ended yet
    bool group_exiting;     // in the leader: every thread is to end
    bool group_stopped;     // in the leader: the process is stopped
    struct process *exec_thread; // in the leader: the thread in execve,
                                 // which ends the others; or NULL
    int32_t group_status;   // in the leader: the wait status they end with
    uintptr_t tls;          // the thread pointer (arch_set_tls), or 0
    uintptr_t clear_tid;    // a user address set to 0 when the thread ends
                            // (CLONE_CHILD_CLEARTID), or 0
    uint32_t parent_pid;    // the parent group's tgid; PID_NONE for PID 1 (and
                            // for orphans when there is no init, and for a
                            // thread that ended: the kernel reaps them)
    uint32_t pgid;          // process group (job control)
    uint32_t sid;           // session
    uint32_t uid, euid, suid; // real, effective and saved user id
    uint32_t gid, egid, sgid; // real, effective and saved group id
    int32_t nice;           // -20 (most CPU) to 19 (least): the time slice length
    process_state_t state;  // current state
    user_space_t *space;    // the user memory (shared by threads), or NULL
                            // once released
    int32_t exit_status;    // wait status (wait_status.h), valid in ZOMBIE state
    uintptr_t kernel_stack; // lowest address of the kernel stack (kstack.h)
    uintptr_t kernel_esp;   // saved kernel ESP (for context switch)
    uintptr_t entry;        // ELF entry point (used by trampoline on first schedule)
    wait_queue_t child_exit; // where this process waits for a child to exit
    char **argv;            // kernel-heap copy of argv (freed at exit)

    // intrusive doubly-linked list for the scheduler ready queue.
    struct process *ready_next;
    struct process *ready_prev;

    // intrusive singly-linked list for a wait queue (Phase L3).
    // valid only while state == PROC_STATE_BLOCKED and the process sits
    // on some wait_queue_t. separate from the ready links so a process is
    // never on both lists at once.
    struct process *wait_next;
    wait_queue_t *waiting_on; // the queue it sleeps on (BLOCKED), or NULL
    bool interruptible;       // a signal may end the sleep

    signal_state_t signals;   // pending and blocked
    signal_handlers_t *handlers; // what each signal does (shared by threads);
                                 // kept until the slot is freed
    uint32_t syscall_nr;      // the syscall in progress (for a restart)
    uint64_t alarm_deadline;  // in the leader: tick of the SIGALRM alarm, 0
                              // for none
    uint8_t stop_signal;      // the signal that stopped it (STOPPED); in
                              // the leader, the one that stopped the process
    wait_report_t wait_report; // in the leader: a stop or continue for waitpid
    uint32_t utime;           // timer ticks spent in user code
    uint32_t stime;           // timer ticks spent in the kernel for it
    uint32_t cutime;          // utime of reaped children (and theirs)
    uint32_t cstime;          // stime of reaped children (and theirs)
    uintptr_t fault_addr;     // the last bad user access (for the report)
    bool fault_write;

    // the FPU/SSE registers while another process runs (arch_fpu_save)
    uint8_t fpu_state[ARCH_FPU_STATE_SIZE] __attribute__((aligned(16)));
    char name[PROCESS_NAME_MAX]; // the program's file name (procfs)
    fs_context_t *fs;       // cwd, root and umask (shared by threads), or
                            // NULL once released
    fd_table_t *files;      // the descriptors (shared by threads), or NULL
                            // once released
    file_t *held_files[FD_MAX]; // files fd_get keeps open for the syscall in
    uint32_t held_count;        // progress, while the table is shared
} process_t;

/**
 * add a user to an fs context (a new thread that shares it).
 * @return fs.
 */
fs_context_t *fs_context_get(fs_context_t *fs);

/**
 * remove a user from an fs context; the last user frees it.
 * @param fs the context, or NULL (nothing is done).
 */
void fs_context_put(fs_context_t *fs);

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

struct registers;

/**
 * fork the current process. the child is a copy of the caller: the same
 * memory (shared copy-on-write), areas, fds, cwd and break. it is made
 * runnable at once and resumes from the same syscall, which returns 0 in
 * the child.
 * @param regs the caller's saved user registers (its syscall frame).
 * @return the child, or NULL if the process table, a kernel stack or
 *         memory for the page tables ran out.
 */
process_t *process_fork(const struct registers *regs);

/**
 * make a thread of the current process: a new task in its group that
 * shares its memory, descriptors, fs context and handlers. it resumes from
 * the same syscall, which returns 0 there, with stack as its user stack
 * pointer. the caller makes it runnable.
 * @param regs the caller's saved user registers (its syscall frame).
 * @param stack the thread's user stack pointer.
 * @return the thread, or NULL if the table or the memory ran out.
 */
process_t *process_clone_thread(const struct registers *regs, uintptr_t stack);

/**
 * send a signal to the threads of a group (signal_send to each).
 * @param tgid the group.
 * @param except a thread that is left out, or NULL.
 * @param sig the signal.
 */
void process_signal_threads(uint32_t tgid, process_t *except, int sig);

/**
 * make a thread the leader of its group in place of the old leader, which
 * has ended (a zombie) and is the only other member: the thread takes the
 * leader's pid and its place for the parent, and the old slot is reaped
 * by the kernel (execve from a thread that is not the leader).
 * @param proc the thread (the current one).
 */
void process_take_leader(process_t *proc);

/**
 * give every other thread of a process's group its user and group ids,
 * its process group and its session (they belong to the process).
 * @param proc the thread whose values the others take.
 */
void process_sync_threads(process_t *proc);

/**
 * choose the thread of a group that takes a signal sent to the process:
 * the leader if it does not block mask, else a thread that does not,
 * else any of them (the leader first).
 * @param tgid the group.
 * @param mask the signal's bit.
 * @return the thread, or NULL if the group has none left.
 */
process_t *process_pick_thread(uint32_t tgid, uint32_t mask);

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
 * detach the children of an exiting group (call when its last thread
 * ends; parent is that thread): zombie children are destroyed
 * now (nobody wants their status), running ones get init (PID 1) as their
 * parent, which reaps them. without init they lose their parent
 * (PID_NONE) and the kernel reaps them when they exit.
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
 * find a child of a process that has something for waitpid: it exited
 * (a zombie), or, with WUNTRACED / WCONTINUED in options, it stopped or
 * continued and that was not reported yet.
 * @param parent the parent.
 * @param pid a child's pid, or -1 for any child.
 * @param options waitpid options (wait_status.h).
 * @param has_child set to true if parent has at least one matching child.
 * @return the child, or NULL if none has anything to report yet.
 */
process_t *process_find_waitable_child(process_t *parent, int32_t pid, uint32_t options,
                                       bool *has_child);

/**
 * list the pids of a process group.
 * @param pgid the group.
 * @param pids receives up to max pids.
 * @param max capacity of pids.
 * @return the number stored.
 */
uint32_t process_list_group(uint32_t pgid, uint32_t *pids, uint32_t max);

/**
 * true if some live process is in the group and the session.
 */
bool process_group_exists(uint32_t pgid, uint32_t sid);

/**
 * call fn for every process in the table, with the table locked and
 * interrupts masked: fn must not sleep.
 * @param fn the function.
 * @param ctx passed to fn.
 */
void process_for_each(void (*fn)(process_t *proc, void *ctx), void *ctx);

/**
 * a copy of what procfs shows about a process.
 */
typedef struct
{
    uint32_t pid;
    uint32_t parent_pid;
    process_state_t state;
    char name[PROCESS_NAME_MAX];
    uint32_t areas;    // memory areas (VMAs)
    uint32_t open_fds;
    uintptr_t brk;
    uint32_t uid;
    uint32_t utime;    // timer ticks in user code
    uint32_t stime;    // timer ticks in the kernel for it
    uint32_t pages;    // pages of RAM it has mapped (its resident memory)
} process_info_t;

/**
 * list the pids in use.
 * @param pids receives up to max pids, in table order.
 * @param max capacity of pids.
 * @return the number stored.
 */
uint32_t process_list_pids(uint32_t *pids, uint32_t max);

/**
 * copy what procfs shows about a process, safely against its exit.
 * @param pid the process.
 * @param info receives the copy.
 * @return false if no such process exists.
 */
bool process_info(uint32_t pid, process_info_t *info);

/**
 * set a process's name from a program path (its last component).
 * @param proc the process.
 * @param path the program's path.
 */
void process_set_name(process_t *proc, const char *path);

/**
 * the timer ticks the CPU spent in the idle loop since boot.
 */
uint64_t process_idle_ticks(void);

/**
 * charge one timer tick to the current process (timer interrupt).
 * @param user true if user code was running.
 */
void process_account_tick(bool user);

/**
 * give a new process the user and group ids and umask of another.
 */
void process_copy_credentials(process_t *child, const process_t *parent);

/**
 * look up a process by PID. the pointer stays valid only while the process
 * cannot be destroyed (it is the caller, or the caller's unreaped child).
 * @param pid the process identifier.
 * @return pointer to the process, or NULL if not found.
 */
process_t *process_get(uint32_t pid);

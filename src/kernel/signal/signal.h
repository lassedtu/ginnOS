#pragma once

/**
 * signal numbers. they are ABI: the same numbers are in the libc's
 * <signal.h>, and a process killed by a signal reports its number in its
 * wait status (wait_status.h).
 */

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23 // urgent data on a socket (ignored by default)
#define SIGWINCH  28 // the terminal's window size changed (ignored by default)

// signals are numbered 1 to NSIG - 1
#define NSIG 32

#include "common/stdint.h"

// the bit of a signal in a mask
#define SIG_BIT(sig) (1u << (sig))

// signals that cannot be caught, blocked or ignored
#define SIG_UNBLOCKABLE (SIG_BIT(SIGKILL) | SIG_BIT(SIGSTOP))

// handler values with a meaning of their own
#define SIG_DFL 0 // the default action
#define SIG_IGN 1 // ignore

// sigaction flags (ABI, as in libc signal.h)
#define SA_NOCLDSTOP 0x00000001 // no SIGCHLD when a child stops or continues
#define SA_RESTORER  0x04000000 // restorer holds the return trampoline
#define SA_RESTART   0x10000000 // restart an interrupted syscall
#define SA_NODEFER   0x40000000 // do not block the signal in its handler
#define SA_RESETHAND 0x80000000 // back to SIG_DFL once the handler runs

// sigprocmask how values
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/**
 * what a process does with one signal (ABI: the sigaction syscall copies
 * it to and from user space in this layout).
 */
typedef struct
{
    uintptr_t handler;  // SIG_DFL, SIG_IGN or a user function
    uint32_t flags;     // SA_*
    uintptr_t restorer; // user code that calls sigreturn
    uint32_t mask;      // signals blocked while the handler runs
} ksigaction_t;

/**
 * the signal state of a process (inside process_t).
 */
typedef struct
{
    uint32_t pending;        // signals sent and not yet acted on
    uint32_t blocked;        // signals held back
    uint32_t saved_blocked;  // the mask to restore after sigsuspend
    bool restore_blocked;    // saved_blocked is set
    ksigaction_t actions[NSIG];
} signal_state_t;

struct process;
struct registers;

/**
 * send a signal to a process. a signal the process ignores is dropped; a
 * deliverable one wakes the process from an interruptible sleep. SIGCONT
 * continues a stopped process, SIGKILL wakes one. safe in any context.
 * @param proc the process.
 * @param sig the signal (1 to NSIG - 1).
 */
void signal_send(struct process *proc, int sig);

/**
 * send a signal to every process of a process group.
 * @param pgid the group.
 * @param sig the signal.
 * @return the number of processes it went to.
 */
int signal_send_group(uint32_t pgid, int sig);

/**
 * send a signal caused by the process's own instruction (a fault). it is
 * delivered even if blocked or ignored: blocked or ignored, it kills.
 * @param proc the process.
 * @param sig the signal.
 */
void signal_force(struct process *proc, int sig);

/**
 * true if a signal waits that the process does not block. a sleeping
 * syscall that sees this gives up its wait (EINTR or a restart).
 * @param proc the process (NULL: false).
 */
bool signal_pending(const struct process *proc);

/**
 * act on pending signals before returning to user mode: run the default
 * action, or set up the user handler's frame. also turns an interrupted
 * syscall's -ERESTARTSYS into EINTR or a restart. called at the end of
 * every interrupt and syscall that came from user mode.
 * @param regs the user registers about to be restored.
 */
void signal_deliver(struct registers *regs);

/**
 * the signal state of a forked child: the parent's actions and mask, no
 * pending signals.
 */
void signal_fork(struct process *child, const struct process *parent);

/**
 * the signal state after execve: handlers go back to SIG_DFL (ignored
 * signals stay ignored); the mask and pending signals stay.
 */
void signal_exec(struct process *proc);

/**
 * set up signal handling. call once at boot.
 */
void signal_init(void);

/**
 * send SIGALRM to processes whose alarm is due. called from the timer
 * interrupt.
 * @param now the current tick.
 */
void signal_alarm_tick(uint64_t now);

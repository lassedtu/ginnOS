#pragma once

#include <sys/types.h>

/**
 * @file signal.h
 * @brief signals: kill, sigaction, masks.
 *
 * the numbers, flags and the kernel's sigaction layout are ABI and must
 * match src/kernel/signal/signal.h exactly.
 */

typedef unsigned int sigset_t;
typedef int sig_atomic_t;

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
#define SIGVTALRM 26 // a virtual timer ran out (no such timers yet)
#define SIGPROF   27 // a profiling timer ran out (no such timers yet)
#define SIGWINCH  28 // the terminal's window size changed (ignored by default)
#define NSIG      32

typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

// sa_flags
#define SA_NOCLDSTOP 0x00000001
#define SA_RESTORER  0x04000000 // set by the libc
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

// sigprocmask how
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

struct sigaction
{
    sighandler_t sa_handler; // SIG_DFL, SIG_IGN or a function
    int sa_flags;            // SA_*
    void (*sa_restorer)(void); // filled in by sigaction()
    sigset_t sa_mask;        // signals blocked while the handler runs
};

/**
 * read and change what the process does with a signal.
 * @return 0, or -1 (errno set; EINVAL for SIGKILL and SIGSTOP).
 */
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);

/**
 * set a handler (with SA_RESTART, as BSD signal() does).
 * @return the old handler, or SIG_ERR.
 */
sighandler_t signal(int sig, sighandler_t handler);

/**
 * send a signal to a process (pid > 0) or every process but init (-1).
 * @return 0, or -1 (errno set).
 */
int kill(pid_t pid, int sig);

/**
 * send a signal to the calling process.
 */
int raise(int sig);

/**
 * read and change the blocked mask.
 */
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

/**
 * the signals that are blocked and pending.
 */
int sigpending(sigset_t *set);

/**
 * wait for a signal with a temporary mask. always returns -1 with EINTR.
 */
int sigsuspend(const sigset_t *mask);

int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int sig);
int sigdelset(sigset_t *set, int sig);
int sigismember(const sigset_t *set, int sig);

#include "signal.h"

#include "errno.h"
#include "syscall.h"
#include "unistd.h"

// the return trampoline for handlers (syscall.asm)
extern void __sigreturn_restorer(void);

/**
 * a negative syscall result sets errno.
 */
static int check(long ret)
{
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return (int)ret;
}

/**
 * the kernel's layout of struct sigaction (kernel/signal/signal.h).
 */
struct ksigaction
{
    unsigned long handler; // pointer-wide
    unsigned int flags;
    unsigned long restorer; // pointer-wide
    unsigned int mask;
};

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact)
{
    struct ksigaction knew;
    struct ksigaction kold;

    if (act)
    {
        knew.handler = (unsigned long)act->sa_handler;
        knew.flags = (unsigned int)act->sa_flags | SA_RESTORER;
        knew.restorer = (unsigned long)__sigreturn_restorer;
        knew.mask = act->sa_mask;
    }

    int ret = check(_syscall(SYS_SIGACTION, sig, act ? (long)&knew : 0, oldact ? (long)&kold : 0,
                             0, 0));
    if (ret == 0 && oldact)
    {
        oldact->sa_handler = (sighandler_t)kold.handler;
        oldact->sa_flags = (int)kold.flags;
        oldact->sa_restorer = (void (*)(void))kold.restorer;
        oldact->sa_mask = kold.mask;
    }
    return ret;
}

sighandler_t signal(int sig, sighandler_t handler)
{
    struct sigaction act;
    struct sigaction old;
    act.sa_handler = handler;
    act.sa_flags = SA_RESTART;
    act.sa_mask = 0;
    if (sigaction(sig, &act, &old) != 0)
    {
        return SIG_ERR;
    }
    return old.sa_handler;
}

int kill(pid_t pid, int sig)
{
    return check(_syscall(SYS_KILL, pid, sig, 0, 0, 0));
}

int raise(int sig)
{
    return kill(getpid(), sig);
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    return check(_syscall(SYS_SIGPROCMASK, how, (long)set, (long)oldset, 0, 0));
}

int sigpending(sigset_t *set)
{
    return check(_syscall(SYS_SIGPENDING, (long)set, 0, 0, 0, 0));
}

int sigsuspend(const sigset_t *mask)
{
    return check(_syscall(SYS_SIGSUSPEND, (long)mask, 0, 0, 0, 0));
}

int sigemptyset(sigset_t *set)
{
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set)
{
    *set = ~0u;
    return 0;
}

/**
 * true for a signal number sigset_t can hold.
 */
static int valid(int sig)
{
    if (sig <= 0 || sig >= NSIG)
    {
        errno = EINVAL;
        return 0;
    }
    return 1;
}

int sigaddset(sigset_t *set, int sig)
{
    if (!valid(sig))
        return -1;
    *set |= 1u << sig;
    return 0;
}

int sigdelset(sigset_t *set, int sig)
{
    if (!valid(sig))
        return -1;
    *set &= ~(1u << sig);
    return 0;
}

int sigismember(const sigset_t *set, int sig)
{
    if (!valid(sig))
        return -1;
    return (*set >> sig) & 1u;
}

unsigned int alarm(unsigned int seconds)
{
    return (long)_syscall(SYS_ALARM, (long)seconds, 0, 0, 0, 0);
}

int pause(void)
{
    sigset_t mask;
    sigprocmask(SIG_BLOCK, 0, &mask);
    return sigsuspend(&mask);
}

#include "signal.h"

#include "arch/arch.h"
#include "arch/x86/cpu/isr.h"
#include "common/memory.h"
#include "common/stddef.h"
#include "common/stdio.h"
#include "kernel/errno.h"
#include "kernel/klog/klog.h"
#include "kernel/process/process.h"
#include "kernel/process/wait_status.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/syscall/syscall.h"
#include "kernel/syscall/syscall_internal.h"
#include "kernel/time/timer.h"
#include "kernel/usermode/uaccess.h"
#include "kernel/usermode/usermode.h"

/**
 * @file signal.c
 * @brief POSIX signals: sending, default actions, user handlers.
 *
 * a signal is a bit in the target's pending mask. it is acted on when the
 * process next returns to user mode (signal_deliver, at the end of every
 * interrupt and syscall from user mode): ignored, a default action
 * (terminate, stop), or a jump to the user's handler on a frame built on
 * the user stack. the handler returns through the restorer libc passes to
 * sigaction, which calls sigreturn to restore the registers saved in the
 * frame.
 *
 * locking: the signal state of a process changes with interrupts masked,
 * since signals are also sent from interrupt handlers (alarms, and later
 * the keyboard).
 */

// the signals whose default action is to stop the process
#define STOP_SIGNALS (SIG_BIT(SIGSTOP) | SIG_BIT(SIGTSTP) | SIG_BIT(SIGTTIN) | SIG_BIT(SIGTTOU))

// the signals whose default action is to do nothing
#define IGNORE_SIGNALS (SIG_BIT(SIGCHLD) | SIG_BIT(SIGCONT) | SIG_BIT(23) | SIG_BIT(SIGWINCH))

// the EFLAGS bits user code may change: CF PF AF ZF SF TF DF OF
#define USER_EFLAGS_MASK 0x0DD5u

// the trap flag and the direction flag, cleared for a handler
#define EFLAGS_TF 0x0100u
#define EFLAGS_DF 0x0400u

// where processes in sigsuspend sleep; only signals wake them
static wait_queue_t sigsuspend_queue;

/**
 * the registers a handler's frame keeps for sigreturn.
 */
typedef struct
{
    struct registers regs; // the user registers (sigreturn keeps cs and ss)
    uint32_t blocked;      // the mask to restore
} sigcontext_t;

/**
 * the frame on the user stack while a handler runs. the handler sees it
 * as a call: a return address (the restorer), then its argument.
 */
typedef struct
{
    uintptr_t return_address;
    intptr_t signal;
    sigcontext_t context;
    // the FPU/SSE registers of the interrupted code (FXSAVE needs 16-byte
    // alignment; the frame itself is aligned to 16)
    uint8_t fpu[ARCH_FPU_STATE_SIZE] __attribute__((aligned(16)));
} sigframe_t;

static bool valid_signal(int sig)
{
    return sig > 0 && sig < NSIG;
}

/**
 * true if the process would do nothing on the signal right now.
 */
static bool ignores(const process_t *proc, int sig)
{
    uintptr_t handler = proc->signals.actions[sig].handler;
    return handler == SIG_IGN || (handler == SIG_DFL && (IGNORE_SIGNALS & SIG_BIT(sig)));
}

/**
 * tell a parent that its child stopped or continued: SIGCHLD (unless the
 * parent asked for none) and a wake in waitpid.
 */
static void notify_parent(process_t *child)
{
    process_t *parent = process_get(child->parent_pid);
    if (!parent)
    {
        return;
    }
    if (!(parent->signals.actions[SIGCHLD].flags & SA_NOCLDSTOP))
    {
        signal_send(parent, SIGCHLD);
    }
    wait_queue_wake_all(&parent->child_exit);
}

/**
 * make a stopped process run again. interrupts masked.
 */
static void continue_process(process_t *proc, bool report)
{
    proc->stop_signal = 0;
    scheduler_ready(proc);
    if (report)
    {
        proc->wait_report = WAIT_REPORT_CONTINUED;
        notify_parent(proc);
    }
}

void signal_send(process_t *proc, int sig)
{
    if (!proc || !valid_signal(sig))
    {
        return;
    }

    uint32_t flags = arch_irq_save();
    signal_state_t *state = &proc->signals;

    if (proc->state == PROC_STATE_UNUSED || proc->state == PROC_STATE_ZOMBIE)
    {
        arch_irq_restore(flags);
        return;
    }

    // a continue cancels pending stops and the other way round
    if (sig == SIGCONT)
    {
        state->pending &= ~STOP_SIGNALS;
        if (proc->state == PROC_STATE_STOPPED)
        {
            continue_process(proc, true);
        }
    }
    else if (STOP_SIGNALS & SIG_BIT(sig))
    {
        state->pending &= ~SIG_BIT(SIGCONT);
    }
    else if (sig == SIGKILL && proc->state == PROC_STATE_STOPPED)
    {
        continue_process(proc, false); // to die
    }

    // init takes only the signals it handles; others would end the system
    bool dropped = proc->pid == PID_INIT && state->actions[sig].handler == SIG_DFL;

    // an ignored signal is dropped, unless it is blocked (the action may
    // change before it is unblocked)
    if (ignores(proc, sig) && !(state->blocked & SIG_BIT(sig)))
    {
        dropped = true;
    }

    if (!dropped)
    {
        state->pending |= SIG_BIT(sig);
        if (signal_pending(proc))
        {
            wait_queue_interrupt(proc);
        }
    }

    arch_irq_restore(flags);
}

void signal_force(process_t *proc, int sig)
{
    uint32_t flags = arch_irq_save();
    signal_state_t *state = &proc->signals;

    // a fault that cannot be handled ends the process
    if ((state->blocked & SIG_BIT(sig)) || state->actions[sig].handler == SIG_IGN)
    {
        state->blocked &= ~SIG_BIT(sig);
        state->actions[sig].handler = SIG_DFL;
    }
    state->pending |= SIG_BIT(sig);

    arch_irq_restore(flags);
}

bool signal_pending(const process_t *proc)
{
    if (!proc)
    {
        return false;
    }
    uint32_t blocked = proc->signals.blocked & ~SIG_UNBLOCKABLE;
    return (proc->signals.pending & ~blocked) != 0;
}

/**
 * the lowest pending signal that is not blocked, taken out of the pending
 * mask; 0 if there is none.
 */
static int take_signal(process_t *proc)
{
    uint32_t flags = arch_irq_save();
    uint32_t blocked = proc->signals.blocked & ~SIG_UNBLOCKABLE;
    uint32_t ready = proc->signals.pending & ~blocked;

    int sig = 0;
    if (ready)
    {
        sig = __builtin_ctz(ready);
        proc->signals.pending &= ~SIG_BIT(sig);
    }

    arch_irq_restore(flags);
    return sig;
}

/**
 * stop the current process until SIGCONT (or SIGKILL).
 */
static void stop_current(process_t *proc, int sig)
{
    uint32_t flags = arch_irq_save();
    proc->state = PROC_STATE_STOPPED;
    proc->stop_signal = (uint8_t)sig;
    proc->wait_report = WAIT_REPORT_STOPPED;
    scheduler_remove(proc);
    notify_parent(proc);
    scheduler_yield(); // returns once something continues it
    arch_irq_restore(flags);
}

/**
 * a line on the console for a process a fault signal is about to end.
 */
static void report_fatal(const process_t *proc, int sig)
{
    switch (sig)
    {
    case SIGSEGV:
        printf("pid %u: segmentation fault (%s 0x%lx)\r\n", proc->pid,
               proc->fault_write ? "write to" : "read of", (unsigned long)proc->fault_addr);
        break;
    case SIGFPE:
        printf("pid %u: arithmetic exception\r\n", proc->pid);
        break;
    case SIGILL:
        printf("pid %u: illegal instruction\r\n", proc->pid);
        break;
    case SIGBUS:
        printf("pid %u: bus error\r\n", proc->pid);
        break;
    case SIGTRAP:
        printf("pid %u: trace trap\r\n", proc->pid);
        break;
    default:
        break; // signals sent by others end a process quietly
    }
}

/**
 * point the registers at the syscall instruction again, with its number,
 * so the interrupted syscall runs again on return to user mode.
 */
static void restart_syscall(process_t *proc, struct registers *regs)
{
    REG_RESULT(regs) = proc->syscall_nr;
    REG_IP(regs) -= 2; // the length of "int 0x80" and of "syscall"
}

/**
 * save the registers on the user stack and enter the handler.
 * @return false if the user stack cannot hold the frame.
 */
static bool enter_handler(process_t *proc, struct registers *regs, int sig)
{
    ksigaction_t *action = &proc->signals.actions[sig];

    // the FPU registers go through the process's own aligned save area:
    // FXSAVE needs 16-byte alignment, which the kernel stack does not have
    sigframe_t frame;
    arch_fpu_save(proc->fpu_state);
    memcpy(frame.fpu, proc->fpu_state, sizeof(frame.fpu));
    frame.return_address = action->restorer;
    frame.signal = sig;
    frame.context.regs = *regs;

    // after sigsuspend, the handler's return restores the mask from before
    // the call, not the temporary one
    uint32_t flags = arch_irq_save();
    frame.context.blocked =
        proc->signals.restore_blocked ? proc->signals.saved_blocked : proc->signals.blocked;
    proc->signals.restore_blocked = false;
    arch_irq_restore(flags);

#ifdef __x86_64__
    // do not write over the red zone (128 bytes below RSP that leaf
    // functions use). the handler starts as after a call: RSP + 8 is a
    // multiple of 16. its argument is in RDI.
    uintptr_t frame_addr = ((REG_SP(regs) - 128 - sizeof(sigframe_t)) & ~(uintptr_t)15) - 8;
#else
    uintptr_t frame_addr = (REG_SP(regs) - sizeof(sigframe_t)) & ~(uintptr_t)15;
#endif
    if (kerr_failed(copy_to_user((void *)frame_addr, &frame, sizeof(frame))))
    {
        return false;
    }

    REG_SP(regs) = frame_addr;
    REG_IP(regs) = action->handler;
    REG_FLAGS(regs) &= ~(uintptr_t)(EFLAGS_TF | EFLAGS_DF);
#ifdef __x86_64__
    regs->rdi = (uint64_t)sig;
#endif

    flags = arch_irq_save();
    uint32_t block = action->mask;
    if (!(action->flags & SA_NODEFER))
    {
        block |= SIG_BIT(sig);
    }
    proc->signals.blocked = (proc->signals.blocked | block) & ~SIG_UNBLOCKABLE;
    if (action->flags & SA_RESETHAND)
    {
        action->handler = SIG_DFL;
    }
    arch_irq_restore(flags);
    return true;
}

void signal_deliver(struct registers *regs)
{
    process_t *proc = process_current();
    if (!proc)
    {
        return;
    }

    bool interrupted = regs->interrupt == SYSCALL_VECTOR && REG_RESULT(regs) == (uintptr_t)-ERESTARTSYS;

    int sig;
    while ((sig = take_signal(proc)) != 0)
    {
        ksigaction_t *action = &proc->signals.actions[sig];

        if (action->handler == SIG_IGN)
        {
            continue;
        }

        if (action->handler == SIG_DFL)
        {
            if (IGNORE_SIGNALS & SIG_BIT(sig))
            {
                continue;
            }
            if (STOP_SIGNALS & SIG_BIT(sig))
            {
                stop_current(proc, sig);
                continue;
            }

            // terminate. exiting may sleep (closing files): leave the
            // interrupt gate's masked state behind, nothing returns here.
            report_fatal(proc, sig);
            arch_enable_interrupts();
            usermode_kill(sig);
        }

        // a user handler. a syscall it interrupted ends with EINTR, or
        // runs again after the handler with SA_RESTART
        if (interrupted)
        {
            if (action->flags & SA_RESTART)
            {
                restart_syscall(proc, regs);
            }
            else
            {
                REG_RESULT(regs) = (uintptr_t)-EINTR;
            }
            interrupted = false;
        }

        if (!enter_handler(proc, regs, sig))
        {
            // nowhere to put the frame: the process cannot go on
            proc->fault_addr = REG_SP(regs);
            proc->fault_write = true;
            report_fatal(proc, SIGSEGV);
            arch_enable_interrupts();
            usermode_kill(SIGSEGV);
        }
        return; // the other signals wait until the handler returns
    }

    // no handler ran: an interrupted syscall runs again
    if (interrupted)
    {
        restart_syscall(proc, regs);
    }

    // a sigsuspend whose signal needed no handler: its mask goes back now
    uint32_t flags = arch_irq_save();
    if (proc->signals.restore_blocked)
    {
        proc->signals.blocked = proc->signals.saved_blocked;
        proc->signals.restore_blocked = false;
    }
    arch_irq_restore(flags);
}

void signal_fork(process_t *child, const process_t *parent)
{
    child->signals = parent->signals;
    child->signals.pending = 0;
    child->signals.restore_blocked = false;
    child->alarm_deadline = 0;
}

void signal_exec(process_t *proc)
{
    for (int sig = 1; sig < NSIG; sig++)
    {
        ksigaction_t *action = &proc->signals.actions[sig];
        if (action->handler != SIG_IGN)
        {
            action->handler = SIG_DFL;
        }
        action->flags = 0;
        action->restorer = 0;
        action->mask = 0;
    }
}

/**
 * collect the processes whose alarm is due (process_for_each callback).
 */
typedef struct
{
    uint64_t now;
    uint32_t pids[PROCESS_MAX];
    uint32_t count;
} due_alarms_t;

static void collect_alarm(process_t *proc, void *ctx)
{
    due_alarms_t *due = ctx;
    if (proc->alarm_deadline != 0 && proc->alarm_deadline <= due->now)
    {
        proc->alarm_deadline = 0;
        due->pids[due->count++] = proc->pid;
    }
}

void signal_alarm_tick(uint64_t now)
{
    // collected with the table locked, sent after it is free again
    due_alarms_t due;
    due.now = now;
    due.count = 0;
    process_for_each(collect_alarm, &due);

    for (uint32_t i = 0; i < due.count; i++)
    {
        signal_send(process_get(due.pids[i]), SIGALRM);
    }
}

int signal_send_group(uint32_t pgid, int sig)
{
    uint32_t pids[PROCESS_MAX];
    uint32_t count = process_list_group(pgid, pids, PROCESS_MAX);
    for (uint32_t i = 0; i < count; i++)
    {
        signal_send(process_get(pids[i]), sig);
    }
    return (int)count;
}

/*
 * the syscalls
 */

/**
 * SYS_kill: send a signal.
 * args: EBX = pid: a process (> 0), the caller's process group (0), every
 *             process but init (-1), or the process group -pid (< -1);
 *       ECX = signal (0 only checks that the target exists).
 * returns 0, or -ESRCH, -EINVAL.
 */
syscall_ret_t sys_kill(struct registers *regs)
{
    int32_t pid = (int32_t)syscall_arg(regs, 0);
    int sig = (int)syscall_arg(regs, 1);

    if (sig != 0 && !valid_signal(sig))
        return -EINVAL;

    if (pid > 0)
    {
        process_t *target = process_get((uint32_t)pid);
        if (!target || target->state == PROC_STATE_ZOMBIE)
            return -ESRCH;
        signal_send(target, sig);
        return 0;
    }

    if (pid == 0 || pid < -1)
    {
        uint32_t pgid = pid == 0 ? process_current()->pgid : (uint32_t)-pid;
        if (sig == 0)
            return process_list_group(pgid, &(uint32_t){0}, 1) ? 0 : -ESRCH;
        return signal_send_group(pgid, sig) > 0 ? 0 : -ESRCH;
    }

    if (pid == -1)
    {
        uint32_t pids[PROCESS_MAX];
        uint32_t count = process_list_pids(pids, PROCESS_MAX);
        bool sent = false;
        for (uint32_t i = 0; i < count; i++)
        {
            if (pids[i] != PID_INIT)
            {
                signal_send(process_get(pids[i]), sig);
                sent = true;
            }
        }
        return sent ? 0 : -ESRCH;
    }

    return -EINVAL;
}

/**
 * SYS_sigaction: read and change what a process does with a signal.
 * args: EBX = signal, ECX = new action (ksigaction_t, may be NULL),
 *       EDX = old action out (may be NULL).
 * SIGKILL and SIGSTOP cannot be changed. returns 0 or a negative errno.
 */
syscall_ret_t sys_sigaction(struct registers *regs)
{
    int sig = (int)syscall_arg(regs, 0);
    const ksigaction_t *user_new = (const ksigaction_t *)syscall_arg(regs, 1);
    ksigaction_t *user_old = (ksigaction_t *)syscall_arg(regs, 2);
    process_t *proc = process_current();

    if (!valid_signal(sig) || (user_new && (SIG_UNBLOCKABLE & SIG_BIT(sig))))
        return -EINVAL;

    ksigaction_t old = proc->signals.actions[sig];
    ksigaction_t new_action;
    if (user_new && kerr_failed(copy_from_user(&new_action, user_new, sizeof(new_action))))
        return -EFAULT;
    if (user_old && kerr_failed(copy_to_user(user_old, &old, sizeof(old))))
        return -EFAULT;

    if (user_new)
    {
        new_action.mask &= ~SIG_UNBLOCKABLE;
        uint32_t flags = arch_irq_save();
        proc->signals.actions[sig] = new_action;
        // a pending signal that is now ignored is dropped
        if (ignores(proc, sig))
            proc->signals.pending &= ~SIG_BIT(sig);
        arch_irq_restore(flags);
    }
    return 0;
}

/**
 * SYS_sigprocmask: read and change the blocked mask.
 * args: EBX = how (SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK), ECX = set
 *       (uint32_t *, may be NULL), EDX = old set out (may be NULL).
 * returns 0 or a negative errno.
 */
syscall_ret_t sys_sigprocmask(struct registers *regs)
{
    int how = (int)syscall_arg(regs, 0);
    const uint32_t *user_set = (const uint32_t *)syscall_arg(regs, 1);
    uint32_t *user_old = (uint32_t *)syscall_arg(regs, 2);
    process_t *proc = process_current();

    uint32_t old = proc->signals.blocked;
    uint32_t set = 0;
    if (user_set && kerr_failed(copy_from_user(&set, user_set, sizeof(set))))
        return -EFAULT;
    if (user_old && kerr_failed(copy_to_user(user_old, &old, sizeof(old))))
        return -EFAULT;

    if (user_set)
    {
        uint32_t blocked;
        switch (how)
        {
        case SIG_BLOCK:
            blocked = old | set;
            break;
        case SIG_UNBLOCK:
            blocked = old & ~set;
            break;
        case SIG_SETMASK:
            blocked = set;
            break;
        default:
            return -EINVAL;
        }
        proc->signals.blocked = blocked & ~SIG_UNBLOCKABLE;
    }
    return 0;
}

/**
 * SYS_sigpending: the signals that are pending and blocked.
 * args: EBX = uint32_t * out.
 */
syscall_ret_t sys_sigpending(struct registers *regs)
{
    process_t *proc = process_current();
    uint32_t pending = proc->signals.pending & proc->signals.blocked;
    return kerr_to_errno(copy_to_user((void *)syscall_arg(regs, 0), &pending, sizeof(pending)));
}

/**
 * SYS_sigsuspend: wait for a signal with a temporary mask.
 * args: EBX = uint32_t * mask.
 * the mask is in place until a signal comes; after its handler (or its
 * default action) the old mask is back. always returns -EINTR.
 */
syscall_ret_t sys_sigsuspend(struct registers *regs)
{
    uint32_t mask;
    if (kerr_failed(copy_from_user(&mask, (const void *)syscall_arg(regs, 0), sizeof(mask))))
        return -EFAULT;

    process_t *proc = process_current();
    uint32_t flags = arch_irq_save();
    proc->signals.saved_blocked = proc->signals.blocked;
    proc->signals.restore_blocked = true;
    proc->signals.blocked = mask & ~SIG_UNBLOCKABLE;
    while (!signal_pending(proc))
    {
        wait_queue_block_interruptible(&sigsuspend_queue);
    }
    arch_irq_restore(flags);
    return -EINTR;
}

/**
 * SYS_sigreturn: return from a signal handler (called by the restorer).
 * restores the registers and mask saved in the handler's frame; the
 * result is the interrupted code's EAX.
 */
syscall_ret_t sys_sigreturn(struct registers *regs)
{
    process_t *proc = process_current();

    // the handler's ret popped the return address, so the frame starts one
    // word below the stack pointer
    //
    // the FPU registers go through the process's own aligned save area.
    // interrupts stay masked from the copy to the load: a process switch in
    // between would save the CPU's registers over the copy (the copy does
    // not sleep)
    uint8_t *fpu = proc->fpu_state;
    sigcontext_t context;
    uintptr_t frame = REG_SP(regs) - sizeof(uintptr_t);
    const void *user_context = (const void *)(frame + offsetof(sigframe_t, context));
    const void *user_fpu = (const void *)(frame + offsetof(sigframe_t, fpu));

    uint32_t mask_flags = arch_irq_save();
    bool copied = kerr_ok(copy_from_user(&context, user_context, sizeof(context))) &&
                  kerr_ok(copy_from_user(fpu, user_fpu, ARCH_FPU_STATE_SIZE));
    if (copied)
    {
        arch_fpu_sanitize(fpu);
        arch_fpu_restore(fpu);
    }
    arch_irq_restore(mask_flags);

    if (!copied)
    {
        proc->fault_addr = (uintptr_t)user_context;
        proc->fault_write = false;
        signal_force(proc, SIGSEGV);
        return 0;
    }

    // the general registers, the instruction pointer and the stack pointer
    // come from the frame. the segment selectors and the interrupt fields
    // stay as they are, and only the user flags change, so the handler
    // cannot get kernel privileges
    struct registers saved = context.regs;
    saved.interrupt = regs->interrupt;
    saved.error = regs->error;
    saved.cs = regs->cs;
    saved.ss = regs->ss;
#ifndef __x86_64__
    saved.ds = regs->ds;
    saved.kern_esp = regs->kern_esp;
#endif
    REG_FLAGS(&saved) =
        (REG_FLAGS(regs) & ~(uintptr_t)USER_EFLAGS_MASK) | (REG_FLAGS(&saved) & USER_EFLAGS_MASK);
    *regs = saved;

    uint32_t flags = arch_irq_save();
    proc->signals.blocked = context.blocked & ~SIG_UNBLOCKABLE;
    arch_irq_restore(flags);

    // the dispatcher stores the result in REG_RESULT: give back the saved one
    return (syscall_ret_t)REG_RESULT(&saved);
}

/**
 * SYS_alarm: send SIGALRM after a number of seconds (0 cancels).
 * returns the seconds that were left of the previous alarm (rounded up).
 */
syscall_ret_t sys_alarm(struct registers *regs)
{
    uint32_t seconds = (uint32_t)syscall_arg(regs, 0);
    process_t *proc = process_current();
    uint64_t now = timer_now();

    uint32_t flags = arch_irq_save();
    uint64_t old = proc->alarm_deadline;
    proc->alarm_deadline = seconds ? now + (uint64_t)seconds * TIMER_HZ : 0;
    arch_irq_restore(flags);

    if (old == 0 || old <= now)
        return 0;
    uint64_t left = old - now; // ticks; the kernel has no 64-bit division
    uint32_t ticks = left > UINT32_MAX ? UINT32_MAX : (uint32_t)left;
    return (syscall_ret_t)((ticks + TIMER_HZ - 1) / TIMER_HZ);
}

void signal_init(void)
{
    wait_queue_init(&sigsuspend_queue);
}

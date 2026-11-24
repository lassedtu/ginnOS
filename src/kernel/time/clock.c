#include "clock.h"

#include "common/div64.h"
#include "drivers/rtc/rtc.h"
#include "kernel/klog/klog.h"
#include "kernel/time/timer.h"

// the wall-clock time at boot, in seconds since 1970
static uint32_t boot_seconds;

void clock_init(void)
{
    if (!rtc_read_unix_time(&boot_seconds))
    {
        boot_seconds = 0;
        KLOG_WARN("clock: the RTC gives no valid time; the clock starts at 1970");
    }
}

uint32_t clock_realtime_seconds(void)
{
    return boot_seconds + (uint32_t)udiv64_32(timer_now(), TIMER_HZ, NULL);
}

void clock_ticks_to_timespec(uint64_t ticks, struct ktimespec *out)
{
    uint32_t rest;
    out->tv_sec = (long)udiv64_32(ticks, TIMER_HZ, &rest);
    out->tv_nsec = (long)(rest * (NSEC_PER_SEC / TIMER_HZ));
}

/**
 * the realtime clock as a timespec: the boot time plus the ticks since.
 */
void clock_realtime(struct ktimespec *out)
{
    clock_ticks_to_timespec(timer_now(), out);
    out->tv_sec += (long)boot_seconds;
}

/*
 * the syscalls
 */

#include "arch/arch.h"
#include "kernel/errno.h"
#include "kernel/process/process.h"
#include "kernel/syscall/syscall_internal.h"
#include "kernel/usermode/uaccess.h"

// where nanosleep sleeps
static timed_queue_t sleep_queue;

void clock_init_sleep(void)
{
    timed_queue_init(&sleep_queue);
}

/**
 * SYS_clock_settime: set the realtime clock (root only).
 * args: clock id (CLOCK_REALTIME only), struct timespec *.
 * the clock keeps whole seconds; the battery-backed clock (RTC) is not
 * written, so the next boot reads its old time again.
 * returns 0, or -EPERM, -EINVAL (another clock, a bad time), -EFAULT.
 */
syscall_ret_t sys_clock_settime(struct registers *regs)
{
    if ((uint32_t)syscall_arg(regs, 0) != CLOCK_REALTIME)
        return -EINVAL;
    if (process_current()->euid != 0)
        return -EPERM;
    struct ktimespec when;
    if (kerr_failed(copy_from_user(&when, (const void *)syscall_arg(regs, 1), sizeof(when))))
        return -EFAULT;
    uint32_t uptime = (uint32_t)udiv64_32(timer_now(), TIMER_HZ, NULL);
    if (when.tv_sec < (long)uptime || when.tv_nsec < 0 || when.tv_nsec >= (long)NSEC_PER_SEC ||
        (unsigned long)when.tv_sec > UINT32_MAX)
        return -EINVAL;
    boot_seconds = (uint32_t)when.tv_sec - uptime;
    return 0;
}

/**
 * SYS_clock_gettime: read a clock.
 * args: EBX = clock id (CLOCK_*), ECX = struct timespec * out.
 * returns 0, or -EINVAL for an unknown clock, -EFAULT.
 */
syscall_ret_t sys_clock_gettime(struct registers *regs)
{
    uint32_t id = (uint32_t)syscall_arg(regs, 0);
    struct ktimespec now;

    switch (id)
    {
    case CLOCK_REALTIME:
        clock_realtime(&now);
        break;
    case CLOCK_MONOTONIC:
        clock_ticks_to_timespec(timer_now(), &now);
        break;
    case CLOCK_PROCESS_CPUTIME_ID:
    {
        process_t *proc = process_current();
        clock_ticks_to_timespec((uint64_t)proc->utime + proc->stime, &now);
        break;
    }
    default:
        return -EINVAL;
    }

    return kerr_to_errno(copy_to_user((void *)syscall_arg(regs, 1), &now, sizeof(now)));
}

/**
 * the timer ticks that cover at least a time span.
 */
static uint64_t timespec_to_ticks(const struct ktimespec *span)
{
    const uint32_t ns_per_tick = NSEC_PER_SEC / TIMER_HZ;
    uint64_t ticks = (uint64_t)(uint32_t)span->tv_sec * TIMER_HZ;
    uint32_t ns = (uint32_t)span->tv_nsec;
    return ticks + ns / ns_per_tick + (ns % ns_per_tick != 0);
}

/**
 * SYS_nanosleep: sleep for a time span.
 * args: EBX = const struct timespec * span, ECX = struct timespec * left
 *       (may be NULL).
 * the sleep ends at the first timer tick after the span (10 ms steps). a
 * signal ends it early: -EINTR, with the time left in *left.
 * returns 0, or -EINTR, -EINVAL (a negative span or nanoseconds out of
 * range), -EFAULT.
 */
syscall_ret_t sys_nanosleep(struct registers *regs)
{
    struct ktimespec span;
    if (kerr_failed(copy_from_user(&span, (const void *)syscall_arg(regs, 0), sizeof(span))))
        return -EFAULT;
    if (span.tv_sec < 0 || span.tv_nsec < 0 || span.tv_nsec >= (long)NSEC_PER_SEC)
        return -EINVAL;

    process_t *proc = process_current();
    uint64_t deadline = timer_now() + timespec_to_ticks(&span);

    uint32_t flags = arch_irq_save();
    while (timer_now() < deadline)
    {
        if (signal_pending(proc))
        {
            arch_irq_restore(flags);
            struct ktimespec left;
            uint64_t now = timer_now();
            clock_ticks_to_timespec(deadline > now ? deadline - now : 0, &left);
            void *user_left = (void *)syscall_arg(regs, 1);
            if (user_left)
                copy_to_user(user_left, &left, sizeof(left));
            return -EINTR;
        }
        timed_queue_block_interruptible(&sleep_queue, deadline);
    }
    arch_irq_restore(flags);
    return 0;
}

/**
 * struct tms (ABI: four clock_t, in timer ticks).
 */
typedef struct
{
    long tms_utime;
    long tms_stime;
    long tms_cutime;
    long tms_cstime;
} ktms_t;

/**
 * SYS_times: the CPU time of the calling process and its reaped children.
 * args: EBX = struct tms * out (may be NULL).
 * returns the ticks since boot (clock_t, TIMER_HZ per second).
 */
syscall_ret_t sys_times(struct registers *regs)
{
    process_t *proc = process_current();
    void *user_times = (void *)syscall_arg(regs, 0);

    if (user_times)
    {
        ktms_t times = {(long)proc->utime, (long)proc->stime, (long)proc->cutime,
                        (long)proc->cstime};
        if (kerr_failed(copy_to_user(user_times, &times, sizeof(times))))
            return -EFAULT;
    }
    return (syscall_ret_t)(uint32_t)timer_now();
}

#include "syscall.h"
#include "syscall_internal.h"
#include "fd_table.h"
#include "kernel/vfs/poll.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/time/clock.h"

#include "arch/x86/cpu/isr.h"
#include "arch/x86/cpu/idt.h"
#include "arch/x86/cpu/gdt.h"

/**
 * syscall dispatch table. indexed by syscall number (EAX). unimplemented syscalls are NULL.
 */
static syscall_fn_t syscall_table[SYSCALL_COUNT] = {
    [SYS_EXIT] = sys_exit,
    [SYS_WRITE] = sys_write,
    [SYS_READ] = sys_read,
    [SYS_OPEN] = sys_open,
    [SYS_CLOSE] = sys_close,
    [SYS_STAT] = sys_stat,
    [SYS_MKDIR] = sys_mkdir,
    [SYS_EXEC] = sys_exec,
    [SYS_GETPID] = sys_getpid,
    [SYS_WAITPID] = sys_waitpid,
    [SYS_SBRK] = sys_sbrk,
    [SYS_GETCWD] = sys_getcwd,
    [SYS_CHDIR] = sys_chdir,
    [SYS_UNLINK] = sys_unlink,
    [SYS_RMDIR] = sys_rmdir,
    [SYS_PIPE] = sys_pipe,
    [SYS_DUP2] = sys_dup2,
    [SYS_FTRUNCATE] = sys_ftruncate,
    [SYS_LSEEK] = sys_lseek,
    [SYS_POLLKEY] = sys_pollkey,
    [SYS_IOCTL] = sys_ioctl,
    [SYS_MMAP] = sys_mmap,
    [SYS_MUNMAP] = sys_munmap,
    [SYS_MPROTECT] = sys_mprotect,
    [SYS_FORK] = sys_fork,
    [SYS_EXECVE] = sys_execve,
    [SYS_GETPPID] = sys_getppid,
    [SYS_SCHED_YIELD] = sys_sched_yield,
    [SYS_FCNTL] = sys_fcntl,
    [SYS_FSTAT] = sys_fstat,
    [SYS_RENAME] = sys_rename,
    [SYS_ACCESS] = sys_access,
    [SYS_PREAD] = sys_pread,
    [SYS_PWRITE] = sys_pwrite,
    [SYS_FSYNC] = sys_fsync,
    [SYS_GETDENTS] = sys_getdents,
    [SYS_POLL] = sys_poll,
    [SYS_KILL] = sys_kill,
    [SYS_SIGACTION] = sys_sigaction,
    [SYS_SIGPROCMASK] = sys_sigprocmask,
    [SYS_SIGRETURN] = sys_sigreturn,
    [SYS_SIGPENDING] = sys_sigpending,
    [SYS_SIGSUSPEND] = sys_sigsuspend,
    [SYS_ALARM] = sys_alarm,
    [SYS_SETPGID] = sys_setpgid,
    [SYS_GETPGID] = sys_getpgid,
    [SYS_SETSID] = sys_setsid,
    [SYS_GETSID] = sys_getsid,
    [SYS_CLOCK_GETTIME] = sys_clock_gettime,
    [SYS_NANOSLEEP] = sys_nanosleep,
    [SYS_TIMES] = sys_times,
};

/**
 * int 0x80 handler. dispatches to the appropriate syscall based on EAX.
 * arguments are passed in EBX, ECX, EDX, ESI, EDI.
 * return value is placed in EAX.
 */
static void syscall_handler(struct registers *regs)
{
    uint32_t num = regs->eax;

    if (num >= SYSCALL_COUNT || !syscall_table[num])
    {
        regs->eax = (uint32_t)-ENOSYS;
        return;
    }

    // kept for a restart after a signal (signal_deliver)
    process_t *proc = process_current();
    if (proc)
    {
        proc->syscall_nr = num;
    }

    // the return value goes back in EAX
    regs->eax = (uint32_t)syscall_table[num](regs);
}

void syscall_initialize(void)
{
    poll_init();
    signal_init();
    clock_init_sleep();


    /* register the handler in the ISR dispatch table */
    isr_register_handler(SYSCALL_VECTOR, syscall_handler);

    /* overwrite the IDT gate to allow ring 3 invocation.
     * isr_init_gates set this vector as ring 0 interrupt gate;
     * we need it as a ring 3 trap gate (trap so IF stays set). */
    extern void isr_stub_128(void);
    idt_set_gate(
        SYSCALL_VECTOR,
        isr_stub_128,
        GDT_KERNEL_CODE,
        IDT_FLAG_RING3 | IDT_FLAG_GATE_32BIT_TRAP | IDT_FLAG_PRESENT);
}

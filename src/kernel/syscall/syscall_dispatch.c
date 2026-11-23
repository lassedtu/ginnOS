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
    [SYS_GETUID] = sys_getuid,
    [SYS_GETEUID] = sys_geteuid,
    [SYS_GETGID] = sys_getgid,
    [SYS_GETEGID] = sys_getegid,
    [SYS_SETUID] = sys_setuid,
    [SYS_SETGID] = sys_setgid,
    [SYS_SETEUID] = sys_seteuid,
    [SYS_SETEGID] = sys_setegid,
    [SYS_CHMOD] = sys_chmod,
    [SYS_FCHMOD] = sys_fchmod,
    [SYS_CHOWN] = sys_chown,
    [SYS_UMASK] = sys_umask,
    [SYS_REBOOT] = sys_reboot,
    [SYS_UNAME] = sys_uname,
    [SYS_OPENAT] = sys_openat,
    [SYS_MKDIRAT] = sys_mkdirat,
    [SYS_UNLINKAT] = sys_unlinkat,
    [SYS_RENAMEAT] = sys_renameat,
    [SYS_FSTATAT] = sys_fstatat,
    [SYS_FACCESSAT] = sys_faccessat,
    [SYS_FCHMODAT] = sys_fchmodat,
    [SYS_FCHOWNAT] = sys_fchownat,
    [SYS_FCHOWN] = sys_fchown,
    [SYS_SHM_CREATE] = sys_shm_create,
    [SYS_SOCKET] = sys_socket,
    [SYS_BIND] = sys_bind,
    [SYS_LISTEN] = sys_listen,
    [SYS_ACCEPT] = sys_accept,
    [SYS_CONNECT] = sys_connect,
    [SYS_SOCKETPAIR] = sys_socketpair,
    [SYS_SENDMSG] = sys_sendmsg,
    [SYS_RECVMSG] = sys_recvmsg,
};

/**
 * the syscall handler (int 0x80 on i686, the syscall instruction on
 * x86_64). the number is in REG_RESULT, the arguments are read with
 * syscall_arg, and the result goes back in REG_RESULT.
 */
static void syscall_handler(struct registers *regs)
{
    uintptr_t num = REG_RESULT(regs);

    if (num >= SYSCALL_COUNT || !syscall_table[num])
    {
        REG_RESULT(regs) = (uintptr_t)-ENOSYS;
        return;
    }

    // kept for a restart after a signal (signal_deliver)
    process_t *proc = process_current();
    if (proc)
    {
        proc->syscall_nr = (uint32_t)num;
    }

    REG_RESULT(regs) = (uintptr_t)syscall_table[num](regs);
}

#ifdef __x86_64__

#define MSR_EFER   0xC0000080u
#define MSR_STAR   0xC0000081u
#define MSR_LSTAR  0xC0000082u
#define MSR_SFMASK 0xC0000084u
#define EFER_SCE   0x1u

// the flags that syscall clears: TF, IF, DF (the entry runs with
// interrupts off until the frame is on the kernel stack)
#define SYSCALL_FLAGS_MASK 0x700u

extern void syscall_entry(void);

static void write_msr(uint32_t msr, uint64_t value)
{
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static uint64_t read_msr(uint32_t msr)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

/**
 * turn on the syscall instruction. STAR gives the kernel CS (and SS = CS +
 * 8) for syscall and the base for sysret (user SS = base + 8, CS = base +
 * 16); LSTAR is the entry (asm64/syscall.asm).
 */
static void syscall_instruction_init(void)
{
    write_msr(MSR_EFER, read_msr(MSR_EFER) | EFER_SCE);
    write_msr(MSR_STAR, ((uint64_t)GDT_KERNEL_DATA << 48) | ((uint64_t)GDT_KERNEL_CODE << 32));
    write_msr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);
    write_msr(MSR_SFMASK, SYSCALL_FLAGS_MASK);
}

#endif

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

#ifdef __x86_64__
    // x86_64 programs use the syscall instruction (the int 0x80 gate stays)
    syscall_instruction_init();
#endif
}

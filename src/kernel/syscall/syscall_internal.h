#pragma once

#include "arch/x86/cpu/isr.h"
#include "kernel/errno.h"
#include "common/stdint.h"
#include "common/error.h"
#include "kernel/memory/memory_layout.h"
#include "kernel/usermode/uaccess.h"

/**
 * one syscall argument or return value: pointer-width, so a user address
 * passes through unchanged. handlers cast arguments to what they mean.
 */
typedef uintptr_t syscall_arg_t;

/**
 * read syscall argument n (0-based). the only place that knows the register
 * convention: int 0x80 on i686 passes EBX, ECX, EDX, ESI, EDI.
 * @param regs the trap frame of the syscall.
 * @param n argument index, 0 to 4.
 * @return the argument.
 */
static inline syscall_arg_t syscall_arg(const struct registers *regs, int n)
{
    switch (n)
    {
    case 0:
        return regs->ebx;
    case 1:
        return regs->ecx;
    case 2:
        return regs->edx;
    case 3:
        return regs->esi;
    default:
        return regs->edi;
    }
}

/**
 * syscall function pointer type. takes a pointer to the CPU registers struct and returns an int32_t.
 * the registers struct contains the syscall number in EAX and arguments in EBX, ECX, EDX, ESI, EDI.
 * the return value is placed in EAX.
 */
typedef intptr_t syscall_ret_t;
typedef syscall_ret_t (*syscall_fn_t)(struct registers *regs);


/**
 * copy a path string in from user memory and resolve it against the current
 * process's cwd.
 * @param user_path the user's path pointer.
 * @param resolved receives the absolute path.
 * @param size size of resolved.
 * @return 0, or a negative errno (EFAULT for a bad pointer, EINVAL for a
 *         path too long or unresolvable).
 */
int32_t resolve_user_path(const char *user_path, char *resolved, uint32_t size);

/* sys_io.c */
syscall_ret_t sys_write(struct registers *regs);
syscall_ret_t sys_read(struct registers *regs);
syscall_ret_t sys_open(struct registers *regs);
syscall_ret_t sys_close(struct registers *regs);
syscall_ret_t sys_lseek(struct registers *regs);
syscall_ret_t sys_ftruncate(struct registers *regs);
syscall_ret_t sys_dup2(struct registers *regs);
syscall_ret_t sys_pipe(struct registers *regs);

/* sys_fs.c */
syscall_ret_t sys_stat(struct registers *regs);
syscall_ret_t sys_mkdir(struct registers *regs);
syscall_ret_t sys_unlink(struct registers *regs);
syscall_ret_t sys_rmdir(struct registers *regs);
syscall_ret_t sys_getdents(struct registers *regs);
syscall_ret_t sys_poll(struct registers *regs);

/* kernel/signal/signal.c */
syscall_ret_t sys_kill(struct registers *regs);
syscall_ret_t sys_sigaction(struct registers *regs);
syscall_ret_t sys_sigprocmask(struct registers *regs);
syscall_ret_t sys_sigreturn(struct registers *regs);
syscall_ret_t sys_sigpending(struct registers *regs);
syscall_ret_t sys_sigsuspend(struct registers *regs);
syscall_ret_t sys_alarm(struct registers *regs);

/* sys_proc.c */
syscall_ret_t sys_exit(struct registers *regs);
syscall_ret_t sys_exec(struct registers *regs);
syscall_ret_t sys_getpid(struct registers *regs);
syscall_ret_t sys_waitpid(struct registers *regs);
syscall_ret_t sys_sbrk(struct registers *regs);
syscall_ret_t sys_mmap(struct registers *regs);
syscall_ret_t sys_munmap(struct registers *regs);
syscall_ret_t sys_mprotect(struct registers *regs);

/**
 * SYS_fork: create a child process that is a copy of the caller.
 * returns the child's PID in the parent and 0 in the child, or a negative
 * errno.
 */
syscall_ret_t sys_fork(struct registers *regs);

/**
 * SYS_execve: replace the calling process's program with a new one.
 */
syscall_ret_t sys_execve(struct registers *regs);
syscall_ret_t sys_getppid(struct registers *regs);
syscall_ret_t sys_sched_yield(struct registers *regs);
syscall_ret_t sys_fcntl(struct registers *regs);
syscall_ret_t sys_fstat(struct registers *regs);
syscall_ret_t sys_rename(struct registers *regs);
syscall_ret_t sys_access(struct registers *regs);
syscall_ret_t sys_pread(struct registers *regs);
syscall_ret_t sys_pwrite(struct registers *regs);
syscall_ret_t sys_fsync(struct registers *regs);

/* sys_misc.c */
syscall_ret_t sys_getcwd(struct registers *regs);
syscall_ret_t sys_chdir(struct registers *regs);
syscall_ret_t sys_ttyctl(struct registers *regs);
syscall_ret_t sys_ioctl(struct registers *regs);

/* sys_fb.c */
syscall_ret_t sys_pollkey(struct registers *regs);

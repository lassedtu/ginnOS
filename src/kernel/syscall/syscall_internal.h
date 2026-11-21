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
 * convention: int 0x80 on i686 passes EBX, ECX, EDX, ESI, EDI; syscall on
 * x86_64 passes RDI, RSI, RDX, R10, R8, R9 (RCX holds the return address).
 * @param regs the trap frame of the syscall.
 * @param n argument index, 0 to 4 (0 to 5 on x86_64).
 * @return the argument.
 */
static inline syscall_arg_t syscall_arg(const struct registers *regs, int n)
{
    switch (n)
    {
#ifdef __x86_64__
    case 0:
        return regs->rdi;
    case 1:
        return regs->rsi;
    case 2:
        return regs->rdx;
    case 3:
        return regs->r10;
    case 4:
        return regs->r8;
    default:
        return regs->r9;
#else
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
#endif
    }
}

/**
 * syscall function pointer type. a handler reads its arguments with
 * syscall_arg and returns its result; the dispatcher puts the result in
 * REG_RESULT (EAX or RAX).
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

/**
 * give a file the caller has just created its mode (taken through the
 * umask) and the caller's effective user and group as owner.
 * @param path the new file's absolute path.
 * @param mode the permission bits asked for.
 * @return 0, or a negative errno.
 */
int32_t set_new_owner(const char *path, uint32_t mode);

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

/* sys_cred.c */
syscall_ret_t sys_getuid(struct registers *regs);
syscall_ret_t sys_geteuid(struct registers *regs);
syscall_ret_t sys_getgid(struct registers *regs);
syscall_ret_t sys_getegid(struct registers *regs);
syscall_ret_t sys_setuid(struct registers *regs);
syscall_ret_t sys_setgid(struct registers *regs);
syscall_ret_t sys_seteuid(struct registers *regs);
syscall_ret_t sys_setegid(struct registers *regs);
syscall_ret_t sys_chmod(struct registers *regs);
syscall_ret_t sys_fchmod(struct registers *regs);
syscall_ret_t sys_chown(struct registers *regs);
syscall_ret_t sys_umask(struct registers *regs);

/* kernel/time/clock.c */
syscall_ret_t sys_clock_gettime(struct registers *regs);
syscall_ret_t sys_nanosleep(struct registers *regs);
syscall_ret_t sys_times(struct registers *regs);

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
syscall_ret_t sys_setpgid(struct registers *regs);
syscall_ret_t sys_getpgid(struct registers *regs);
syscall_ret_t sys_setsid(struct registers *regs);
syscall_ret_t sys_getsid(struct registers *regs);
syscall_ret_t sys_fcntl(struct registers *regs);
syscall_ret_t sys_fstat(struct registers *regs);
syscall_ret_t sys_rename(struct registers *regs);
syscall_ret_t sys_access(struct registers *regs);
syscall_ret_t sys_pread(struct registers *regs);
syscall_ret_t sys_pwrite(struct registers *regs);
syscall_ret_t sys_fsync(struct registers *regs);

/* sys_misc.c */
syscall_ret_t sys_reboot(struct registers *regs);
syscall_ret_t sys_shm_create(struct registers *regs);
syscall_ret_t sys_getcwd(struct registers *regs);
syscall_ret_t sys_chdir(struct registers *regs);
syscall_ret_t sys_ioctl(struct registers *regs);

/* sys_fb.c */
syscall_ret_t sys_pollkey(struct registers *regs);

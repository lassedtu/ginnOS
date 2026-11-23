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

// the *at calls: a relative path in the working directory, or in an open one
#define AT_FDCWD            (-100)
#define AT_SYMLINK_NOFOLLOW 0x100 // the link itself, not what it points at
#define AT_REMOVEDIR        0x200 // unlinkat: a directory (rmdir)
#define AT_SYMLINK_FOLLOW   0x400 // linkat: what the link points at
#define AT_EACCESS          0x200 // faccessat: with the effective ids

/**
 * as resolve_user_path, with a relative path taken in the directory open
 * as dirfd (AT_FDCWD: the working directory). the symbolic links along the
 * path are replaced by their targets.
 * @param follow also the last name, when it is a link (stat, open); not for
 *        the calls that act on the name itself (unlink, rename, lstat).
 * @return 0, or -EFAULT, -ENAMETOOLONG, -ENOENT (an empty path), -EBADF,
 *         -ENOTDIR (dirfd is no directory), or -ELOOP.
 */
int32_t resolve_user_path_at(int32_t dirfd, const char *user_path, char *resolved, uint32_t size,
                             bool follow);

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
syscall_ret_t sys_flock(struct registers *regs);
syscall_ret_t sys_openat(struct registers *regs);
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
syscall_ret_t sys_fstatat(struct registers *regs);
syscall_ret_t sys_renameat(struct registers *regs);
syscall_ret_t sys_faccessat(struct registers *regs);
syscall_ret_t sys_mkdirat(struct registers *regs);
syscall_ret_t sys_unlinkat(struct registers *regs);
syscall_ret_t sys_symlinkat(struct registers *regs);
syscall_ret_t sys_readlinkat(struct registers *regs);
syscall_ret_t sys_linkat(struct registers *regs);
syscall_ret_t sys_utimensat(struct registers *regs);
syscall_ret_t sys_mknodat(struct registers *regs);
syscall_ret_t sys_chroot(struct registers *regs);
syscall_ret_t sys_clone(struct registers *regs);
syscall_ret_t sys_exit_group(struct registers *regs);
syscall_ret_t sys_gettid(struct registers *regs);
syscall_ret_t sys_tgkill(struct registers *regs);
syscall_ret_t sys_set_tls(struct registers *regs);
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
syscall_ret_t sys_fchmodat(struct registers *regs);
syscall_ret_t sys_fchownat(struct registers *regs);
syscall_ret_t sys_fchown(struct registers *regs);
syscall_ret_t sys_umask(struct registers *regs);

/* kernel/time/clock.c */
syscall_ret_t sys_clock_gettime(struct registers *regs);
syscall_ret_t sys_clock_settime(struct registers *regs);
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
syscall_ret_t sys_getpriority(struct registers *regs);
syscall_ret_t sys_setpriority(struct registers *regs);
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
syscall_ret_t sys_uname(struct registers *regs);
syscall_ret_t sys_sethostname(struct registers *regs);
syscall_ret_t sys_shm_create(struct registers *regs);
syscall_ret_t sys_getcwd(struct registers *regs);
syscall_ret_t sys_chdir(struct registers *regs);
syscall_ret_t sys_ioctl(struct registers *regs);

/* sys_socket.c */
syscall_ret_t sys_socket(struct registers *regs);
syscall_ret_t sys_bind(struct registers *regs);
syscall_ret_t sys_listen(struct registers *regs);
syscall_ret_t sys_accept(struct registers *regs);
syscall_ret_t sys_connect(struct registers *regs);
syscall_ret_t sys_socketpair(struct registers *regs);
syscall_ret_t sys_sendmsg(struct registers *regs);
syscall_ret_t sys_recvmsg(struct registers *regs);

/* sys_fb.c */
syscall_ret_t sys_pollkey(struct registers *regs);

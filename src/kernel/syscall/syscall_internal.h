#pragma once

#include "arch/x86/cpu/isr.h"
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

/* map a kerr_t error to a negative errno value for userspace */
static inline int32_t kerr_to_errno(kerr_t err)
{
    switch (err)
    {
    case KERR_OK:       return 0;
    case KERR_NOMEM:    return -4;  /* ENOMEM */
    case KERR_IO:       return -14; /* EIO */
    case KERR_NOTFOUND: return -5;  /* ENOENT */
    case KERR_PERM:     return -6;  /* EACCES */
    case KERR_INVAL:    return -3;  /* EINVAL */
    case KERR_BUSY:     return -16; /* EBUSY */
    case KERR_NOSPC:    return -15; /* ENOSPC */
    case KERR_RANGE:    return -17; /* ERANGE */
    case KERR_EXIST:    return -9;  /* EEXIST */
    case KERR_ISDIR:    return -10; /* EISDIR */
    case KERR_NOTDIR:   return -11; /* ENOTDIR */
    case KERR_PIPE:     return -7;  /* EPIPE */
    case KERR_NOENT:    return -5;  /* ENOENT */
    case KERR_FAULT:    return -18; /* EFAULT */
    }
    return -3; /* EINVAL as fallback */
}

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
syscall_ret_t sys_create(struct registers *regs);
syscall_ret_t sys_mkdir(struct registers *regs);
syscall_ret_t sys_unlink(struct registers *regs);
syscall_ret_t sys_rmdir(struct registers *regs);
syscall_ret_t sys_readdir(struct registers *regs);

/* sys_proc.c */
syscall_ret_t sys_exit(struct registers *regs);
syscall_ret_t sys_exec(struct registers *regs);
syscall_ret_t sys_getpid(struct registers *regs);
syscall_ret_t sys_waitpid(struct registers *regs);
syscall_ret_t sys_sbrk(struct registers *regs);
syscall_ret_t sys_mmap(struct registers *regs);
syscall_ret_t sys_munmap(struct registers *regs);
syscall_ret_t sys_mprotect(struct registers *regs);

/* sys_misc.c */
syscall_ret_t sys_getcwd(struct registers *regs);
syscall_ret_t sys_chdir(struct registers *regs);
syscall_ret_t sys_ttyctl(struct registers *regs);
syscall_ret_t sys_ioctl(struct registers *regs);

/* sys_fb.c */
syscall_ret_t sys_pollkey(struct registers *regs);

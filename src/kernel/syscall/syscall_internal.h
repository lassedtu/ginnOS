#pragma once

#include "arch/x86/cpu/isr.h"
#include "common/stdint.h"
#include "common/error.h"
#include "kernel/memory/memory_layout.h"

/**
 * syscall function pointer type. takes a pointer to the CPU registers struct and returns an int32_t.
 * the registers struct contains the syscall number in EAX and arguments in EBX, ECX, EDX, ESI, EDI.
 * the return value is placed in EAX.
 */
typedef int32_t (*syscall_fn_t)(struct registers *regs);

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

/* sys_io.c */
int32_t sys_write(struct registers *regs);
int32_t sys_read(struct registers *regs);
int32_t sys_open(struct registers *regs);
int32_t sys_close(struct registers *regs);
int32_t sys_lseek(struct registers *regs);
int32_t sys_ftruncate(struct registers *regs);
int32_t sys_dup2(struct registers *regs);
int32_t sys_pipe(struct registers *regs);

/* sys_fs.c */
int32_t sys_stat(struct registers *regs);
int32_t sys_create(struct registers *regs);
int32_t sys_mkdir(struct registers *regs);
int32_t sys_unlink(struct registers *regs);
int32_t sys_rmdir(struct registers *regs);
int32_t sys_readdir(struct registers *regs);

/* sys_proc.c */
int32_t sys_exit(struct registers *regs);
int32_t sys_exec(struct registers *regs);
int32_t sys_getpid(struct registers *regs);
int32_t sys_waitpid(struct registers *regs);
int32_t sys_sbrk(struct registers *regs);

/* sys_misc.c */
int32_t sys_getcwd(struct registers *regs);
int32_t sys_chdir(struct registers *regs);
int32_t sys_ttyctl(struct registers *regs);

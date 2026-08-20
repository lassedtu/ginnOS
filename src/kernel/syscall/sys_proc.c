#include "syscall_internal.h"
#include "syscall.h"

#include "kernel/vfs/vfs.h"
#include "kernel/usermode/usermode.h"
#include "kernel/process/process.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/memory/pmm.h"
#include "arch/x86/cpu/paging.h"
#include "common/memory.h"
#include "common/string.h"

/**
 * SYS_exit: terminate the current process.
 * arg: EBX = exit code.
 * returns control to the kernel (exec_program caller).
 */
int32_t sys_exit(struct registers *regs)
{
    int32_t code = (int32_t)regs->ebx;

    usermode_exit(code);

    /* unreachable */
    return 0;
}

/**
 * SYS_exec: load and execute an ELF binary.
 * args: EBX = path string pointer, ECX = argv array pointer (may be NULL).
 * does not return on success. returns negative errno on failure.
 */
int32_t sys_exec(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;
    const char **argv = (const char **)regs->ecx;

    if (!is_user_str(path))
        return -18; /* EFAULT */

    /* resolve relative paths against the process's cwd */
    char resolved[PATH_MAX];
    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, sizeof(resolved)))
    {
        return -3; /* EINVAL */
    }

    int32_t ret = (int32_t)exec_program(resolved, argv);
    if (ret < 0)
    {
        return -5; /* ENOENT */
    }
    return ret;
}

/**
 * SYS_getpid: return the current process ID.
 */
int32_t sys_getpid(struct registers *regs)
{
    (void)regs;
    process_t *proc = process_current();
    if (!proc)
    {
        return -1;
    }
    return (int32_t)proc->pid;
}

/**
 * SYS_waitpid: wait for a child process to exit.
 * args: EBX = child PID to wait for.
 * returns the child's exit code on success, or negative errno on error.
 *
 * if the child is already a zombie, reaps it immediately.
 * otherwise, blocks the calling process until the child exits.
 */
int32_t sys_waitpid(struct registers *regs)
{
    uint32_t child_pid = regs->ebx;
    process_t *parent = process_current();

    if (!parent)
    {
        return -3; /* EINVAL */
    }

    process_t *child = process_get(child_pid);
    if (!child)
    {
        return -19; /* ESRCH */
    }

    /* verify this is actually our child */
    if (child->parent_pid != parent->pid)
    {
        return -20; /* ECHILD */
    }

    /* if child is already a zombie, reap immediately */
    if (child->state == PROC_STATE_ZOMBIE)
    {
        int32_t code = child->exit_code;
        process_destroy(child);
        return code;
    }

    /* child is still running block the parent */
    parent->state = PROC_STATE_BLOCKED;
    parent->wait_for_pid = child_pid;
    scheduler_remove(parent);
    scheduler_yield();

    /* we've been woken up child should now be a zombie */
    child = process_get(child_pid);
    if (child && child->state == PROC_STATE_ZOMBIE)
    {
        int32_t code = child->exit_code;
        process_destroy(child);
        return code;
    }

    /* child disappeared somehow */
    return -19; /* ESRCH */
}

/**
 * SYS_sbrk: grow the user program break.
 * args: EBX = increment (signed; only positive values supported for now).
 * returns the previous break address on success, or (uint32_t)-1 on failure.
 *
 * if increment is 0, simply returns the current break without changing it.
 * for positive increments, allocates physical pages and maps them as
 * user-accessible for any new pages between the old and new break.
 */
int32_t sys_sbrk(struct registers *regs)
{
    int32_t increment = (int32_t)regs->ebx;
    uint32_t old_brk = usermode_get_brk();

    /* increment of 0: just return current break */
    if (increment == 0)
    {
        return (int32_t)old_brk;
    }

    /* negative increment not supported yet */
    if (increment < 0)
    {
        return (int32_t)(uint32_t)-1;
    }

    uint32_t new_brk = old_brk + (uint32_t)increment;

    /* overflow check */
    if (new_brk < old_brk)
    {
        return (int32_t)(uint32_t)-1;
    }

    /* allocate and map any new pages between old_brk and new_brk */
    uint32_t page = old_brk & ~(PAGE_SIZE - 1);
    uint32_t end = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    process_t *proc = process_current();
    uint32_t pd = proc ? proc->page_directory : paging_directory_address();

    for (; page < end; page += PAGE_SIZE)
    {
        void *frame = pmm_alloc_page();
        if (!frame)
        {
            return (int32_t)(uint32_t)-1;
        }

        memset(frame, 0, PAGE_SIZE);
        paging_map_in(pd, page, (uint32_t)frame, PTE_USER_RW);
    }

    usermode_set_brk(new_brk);
    return (int32_t)old_brk;
}

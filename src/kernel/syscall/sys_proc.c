#include "kernel/memory/shm.h"
#include "syscall_internal.h"
#include "syscall.h"

#include "kernel/vfs/vfs.h"
#include "kernel/usermode/usermode.h"
#include "kernel/process/process.h"
#include "kernel/process/wait_status.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pmm_layout.h"
#include "kernel/memory/mman.h"
#include "kernel/memory/mm.h"
#include "kernel/memory/memory_layout.h"
#include "fd_table.h"
#include "arch/arch.h"
#include "common/memory.h"
#include "common/string.h"
#include "kernel/memory/heap.h"

/**
 * SYS_exit: terminate the current process.
 * arg: EBX = exit code.
 * never returns: the process becomes a zombie until its parent reaps it.
 */
syscall_ret_t sys_exit(struct registers *regs)
{
    int32_t code = (int32_t)syscall_arg(regs, 0);

    usermode_exit(code);

    /* unreachable */
    return 0;
}

/**
 * the argv and envp of one exec, copied in from the user: the strings one
 * after the other, and where each starts. strings and offsets grow as
 * they fill; together (a pointer counted for each entry) they may take
 * USER_ARG_MAX bytes.
 */
typedef struct
{
    char *strings;
    size_t used;
    size_t room;
    size_t *starts;   // an offset in strings for each entry, NO_ENTRY ends a vector
    size_t count;     // entries in starts, the two ends included
    size_t capacity;
    size_t argc;
} exec_args_t;

#define NO_ENTRY ((size_t)-1)

static void exec_args_free(exec_args_t *a)
{
    kfree(a->strings);
    kfree(a->starts);
}

/** room for one more entry in starts */
static int32_t add_start(exec_args_t *a, size_t start)
{
    if (a->count == a->capacity)
    {
        size_t bigger = a->capacity ? a->capacity * 2 : 64;
        size_t *grown = krealloc(a->starts, bigger * sizeof(size_t));
        if (!grown)
        {
            return -ENOMEM;
        }
        a->starts = grown;
        a->capacity = bigger;
    }
    a->starts[a->count++] = start;
    return 0;
}

/**
 * copy one string in at the end of strings, making strings larger (up to
 * USER_ARG_MAX) when it does not fit.
 * @return 0, or -EFAULT, -E2BIG or -ENOMEM.
 */
static int32_t copy_string_from_user(exec_args_t *a, const char *entry)
{
    for (;;)
    {
        int32_t len = strncpy_from_user(a->strings + a->used, entry, a->room - a->used);
        if (len == KERR_FAULT)
        {
            return -EFAULT;
        }
        if (len >= 0)
        {
            int32_t err = add_start(a, a->used);
            if (err < 0)
            {
                return err;
            }
            a->used += (size_t)len + 1;
            return 0;
        }
        if (a->room >= USER_ARG_MAX)
        {
            return -E2BIG;
        }
        size_t bigger = a->room * 2 < USER_ARG_MAX ? a->room * 2 : USER_ARG_MAX;
        char *grown = krealloc(a->strings, bigger);
        if (!grown)
        {
            return -ENOMEM;
        }
        a->strings = grown;
        a->room = bigger;
    }
}

/**
 * copy a user string vector (argv or envp) and its strings in.
 * @param user_vector the user's NULL-terminated array (may itself be NULL).
 * @return the entry count, or a negative errno (EFAULT for bad memory,
 *         E2BIG when the arguments pass USER_ARG_MAX, ENOMEM).
 */
static int32_t copy_vector_from_user(const char **user_vector, exec_args_t *a)
{
    int32_t count = 0;
    while (user_vector)
    {
        const char *entry;
        if (kerr_failed(copy_from_user(&entry, &user_vector[count], sizeof(entry))))
        {
            return -EFAULT;
        }
        if (!entry)
        {
            break;
        }
        int32_t err = copy_string_from_user(a, entry);
        if (err < 0)
        {
            return err;
        }
        count++;
        if (a->used + a->count * sizeof(char *) > USER_ARG_MAX)
        {
            return -E2BIG;
        }
    }
    int32_t err = add_start(a, NO_ENTRY);
    return err < 0 ? err : count;
}

/**
 * copy argv and envp (may be NULL) in. on success *vectors is one array:
 * argv's pointers, NULL, envp's pointers, NULL (kfree it, then
 * exec_args_free); *envp_out points at its envp part.
 * @return 0 or a negative errno.
 */
static int32_t copy_args_from_user(const char **argv, const char **envp, exec_args_t *a,
                                   const char ***vectors, const char ***envp_out)
{
    a->room = 4096;
    a->strings = kmalloc(a->room);
    if (!a->strings)
    {
        return -ENOMEM;
    }
    int32_t ret = copy_vector_from_user(argv, a);
    if (ret < 0)
    {
        return ret;
    }
    a->argc = (size_t)ret;
    ret = copy_vector_from_user(envp, a);
    if (ret < 0)
    {
        return ret;
    }
    const char **v = kmalloc(a->count * sizeof(char *));
    if (!v)
    {
        return -ENOMEM;
    }
    for (size_t i = 0; i < a->count; i++)
    {
        v[i] = a->starts[i] == NO_ENTRY ? NULL : a->strings + a->starts[i];
    }
    *vectors = v;
    *envp_out = v + a->argc + 1;
    return 0;
}

/**
 * SYS_exec: start an ELF binary as a child process (spawn, not execve).
 * args: EBX = path string pointer, ECX = argv array pointer (may be NULL).
 * returns the child's PID, or negative errno on failure.
 */
syscall_ret_t sys_exec(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);
    const char **argv = (const char **)syscall_arg(regs, 1);

    /* copy the path in from user memory, resolved against the cwd */
    char resolved[PATH_MAX];
    int32_t path_err = resolve_user_path(path, resolved, sizeof(resolved));
    if (path_err < 0)
        return path_err;

    /* argv and its strings are user memory too: copy them in first */
    exec_args_t args = {0};
    const char **kargv = NULL;
    const char **kenvp;
    int32_t ret = copy_args_from_user(argv, NULL, &args, &kargv, &kenvp);
    if (ret >= 0)
    {
        ret = (int32_t)exec_program(resolved, kargv);
        if (ret < 0)
            ret = -ENOENT;
    }
    kfree(kargv);
    exec_args_free(&args);
    return ret;
}

/**
 * SYS_execve: replace the calling process's program.
 * args: EBX = path, ECX = argv, EDX = envp (both NULL-terminated, may be
 *       NULL).
 * on success it does not return: the syscall frame now enters the new
 * program (exec_replace). returns a negative errno on failure, with the
 * old program still running.
 */
syscall_ret_t sys_execve(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);
    const char **argv = (const char **)syscall_arg(regs, 1);
    const char **envp = (const char **)syscall_arg(regs, 2);

    char resolved[PATH_MAX];
    int32_t ret = resolve_user_path(path, resolved, sizeof(resolved));
    if (ret < 0)
        return ret;

    exec_args_t args = {0};
    const char **kargv = NULL;
    const char **kenvp = NULL;
    ret = copy_args_from_user(argv, envp, &args, &kargv, &kenvp);
    if (ret >= 0)
        ret = kerr_to_errno(exec_replace(regs, resolved, kargv, kenvp));
    kfree(kargv);
    exec_args_free(&args);

    // on success the frame's EAX was cleared for the new program; keep it
    return ret == 0 ? (syscall_ret_t)REG_RESULT(regs) : ret;
}

/**
 * SYS_fork: create a child process that is a copy of the caller.
 * no arguments. returns the child's PID in the parent, 0 in the child (set
 * up by process_fork), or -ENOMEM.
 */
syscall_ret_t sys_fork(struct registers *regs)
{
    process_t *child = process_fork(regs);
    if (!child)
    {
        return -ENOMEM;
    }
    return (syscall_ret_t)child->pid;
}

/**
 * SYS_getpid: return the current process ID.
 */
syscall_ret_t sys_getpid(struct registers *regs)
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
 * SYS_waitpid: wait for a child process to exit and reap it.
 * args: EBX = pid (a child's pid, or -1 for any child),
 *       ECX = int *status (may be NULL; receives the wait status,
 *             wait_status.h), EDX = options (WNOHANG).
 * with WUNTRACED a stopped child and with WCONTINUED a continued one is
 * reported (once) too, without reaping it.
 * returns the child's pid; 0 with WNOHANG when no matching child has
 * anything to report yet; -ECHILD if there is no matching child; -EINVAL for a
 * pid of 0 or below -1 (process groups come later); -EFAULT for a bad
 * status pointer (the child is reaped anyway).
 */
syscall_ret_t sys_waitpid(struct registers *regs)
{
    int32_t pid = (int32_t)syscall_arg(regs, 0);
    int32_t *user_status = (int32_t *)syscall_arg(regs, 1);
    uint32_t options = (uint32_t)syscall_arg(regs, 2);
    process_t *parent = process_current();

    if (!parent || pid == 0 || pid < -1 || (options & ~(uint32_t)(WNOHANG | WUNTRACED | WCONTINUED)))
    {
        return -EINVAL;
    }

    // masked from the search until blocked: a child that exits in between
    // would wake an empty queue, and this process would sleep forever
    uint32_t flags = arch_irq_save();
    process_t *child;
    for (;;)
    {
        bool has_child;
        child = process_find_waitable_child(parent, pid, options, &has_child);
        if (child)
        {
            break;
        }
        if (!has_child)
        {
            arch_irq_restore(flags);
            return -ECHILD;
        }
        if (options & WNOHANG)
        {
            arch_irq_restore(flags);
            return 0;
        }
        if (signal_pending(parent))
        {
            arch_irq_restore(flags);
            return -ERESTARTSYS;
        }
        wait_queue_block_interruptible(&parent->child_exit);
    }

    // a stop or a continue is reported once; an exit reaps the child
    int32_t status;
    uint32_t child_pid = child->pid;
    if (child->state == PROC_STATE_ZOMBIE)
    {
        status = child->exit_status;
        // the child's CPU time, and its children's, counts as the parent's
        parent->cutime += child->utime + child->cutime;
        parent->cstime += child->stime + child->cstime;
        arch_irq_restore(flags);
        process_destroy(child);
    }
    else
    {
        status = child->wait_report == WAIT_REPORT_STOPPED ? WAIT_STATUS_STOPPED(child->stop_signal)
                                                           : WAIT_STATUS_CONTINUED;
        child->wait_report = WAIT_REPORT_NONE;
        arch_irq_restore(flags);
    }

    if (user_status && kerr_failed(copy_to_user(user_status, &status, sizeof(status))))
    {
        return -EFAULT;
    }
    return (syscall_ret_t)child_pid;
}

/**
 * SYS_getppid: return the parent's pid, or 0 if the parent has exited.
 */
syscall_ret_t sys_getppid(struct registers *regs)
{
    (void)regs;
    process_t *proc = process_current();
    return proc ? (syscall_ret_t)proc->parent_pid : 0;
}

/**
 * SYS_sched_yield: let other ready processes run first. returns 0.
 */
syscall_ret_t sys_sched_yield(struct registers *regs)
{
    (void)regs;
    scheduler_yield();
    return 0;
}

/**
 * SYS_sbrk: grow the user program break.
 * args: EBX = increment (signed; only positive values supported for now).
 * returns the previous break address on success, or -1 on failure.
 *
 * if increment is 0, simply returns the current break without changing it.
 * for positive increments, grows the heap area; the new pages are backed
 * with zeroed frames on first touch.
 */
syscall_ret_t sys_sbrk(struct registers *regs)
{
    intptr_t increment = (intptr_t)syscall_arg(regs, 0);
    uintptr_t old_brk = usermode_get_brk();

    /* increment of 0: just return current break */
    if (increment == 0)
    {
        return (syscall_ret_t)old_brk;
    }

    /* negative increment not supported yet */
    if (increment < 0)
    {
        return -1;
    }

    uintptr_t new_brk = old_brk + (uintptr_t)increment;

    /* overflow check */
    if (new_brk < old_brk)
    {
        return -1;
    }

    process_t *proc = process_current();
    if (!proc)
    {
        return -1;
    }

    /* pages up to PAGE_ALIGN_UP(old_brk) are already mapped (the page
     * holding old_brk may contain earlier allocations): map only the pages
     * from there to PAGE_ALIGN_UP(new_brk). */
    uintptr_t page = PAGE_ALIGN_UP(old_brk);
    uintptr_t end = PAGE_ALIGN_UP(new_brk);

    if (end > page)
    {
        /* the heap area grows to cover the new pages; it may not run into
         * another area (the stack, an mmap) */
        if (!vma_range_free(&proc->vmas, page, end))
        {
            return -1;
        }

        vma_t *heap = vma_find_kind(&proc->vmas, VMA_HEAP);
        if (heap && heap->end == page)
        {
            heap->end = end;
        }
        else if (kerr_failed(vma_insert(&proc->vmas, page, end, PROT_READ | PROT_WRITE,
                                        VMA_HEAP)))
        {
            return -1;
        }
    }

    /* the new pages are backed on first touch (demand paging) */
    usermode_set_brk(new_brk);
    return (syscall_ret_t)old_brk;
}

/**
 * SYS_mmap: map memory into the calling process.
 * args: EBX = addr, ECX = length, EDX = prot | flags << 8, ESI = fd,
 *       EDI = offset (prot and flags share a register: all five are taken).
 *
 * two forms:
 *   - anonymous (MAP_ANONYMOUS, fd < 0): zero-filled pages, backed on first
 *     touch.
 *   - device-backed (fd is an open device node): route through vfs_mmap so the
 *     device maps its own memory (e.g. the framebuffer's LFB).
 * with MAP_FIXED the mapping is placed exactly at addr (page-aligned, user
 * space), replacing whatever was mapped there. otherwise addr is a hint,
 * used if that range of the mmap region is free; failing that, the lowest
 * free range of the region is used.
 * returns the mapped address, or MAP_FAILED (-1) on error.
 */
syscall_ret_t sys_mmap(struct registers *regs)
{
    uintptr_t hint = syscall_arg(regs, 0);
    size_t length = syscall_arg(regs, 1);
    uint32_t prot_flags = (uint32_t)syscall_arg(regs, 2);
    int prot = (int)(prot_flags & 0xFFu);
    int flags = (int)(prot_flags >> 8);
    int32_t fd = (int32_t)syscall_arg(regs, 3);
    uint32_t offset = (uint32_t)syscall_arg(regs, 4);

    process_t *proc = process_current();
    size_t len_pages = PAGE_ALIGN_UP(length);
    if (!proc || length == 0 || len_pages == 0)
    {
        return (syscall_ret_t)MAP_FAILED; // empty, or so large it wrapped
    }

    // offset must be page-aligned (file/device mappings start on a page).
    if (!PAGE_IS_ALIGNED(offset))
    {
        return (syscall_ret_t)MAP_FAILED;
    }

    // a device mapping needs an open device node behind fd
    fd_entry_t *entry = NULL;
    if (!(flags & MAP_ANONYMOUS))
    {
        entry = (fd >= 0) ? fd_get(fd) : NULL;
        if (!entry)
        {
            return (syscall_ret_t)MAP_FAILED;
        }
    }

    // choose the range, then record it as an area before mapping anything
    uintptr_t virt;
    if (flags & MAP_FIXED)
    {
        if (!PAGE_IS_ALIGNED(hint) || hint < USER_SPACE_START ||
            len_pages > USER_SPACE_END - hint)
        {
            return (syscall_ret_t)MAP_FAILED;
        }
        if (kerr_failed(mm_unmap(proc, hint, hint + len_pages)))
        {
            return (syscall_ret_t)MAP_FAILED;
        }
        virt = hint;
    }
    else if (hint != 0 && PAGE_IS_ALIGNED(hint) && hint >= USER_MMAP_BASE &&
             len_pages <= USER_MMAP_END - hint &&
             vma_range_free(&proc->vmas, hint, hint + len_pages))
    {
        virt = hint;
    }
    else
    {
        virt = vma_find_free(&proc->vmas, len_pages, USER_MMAP_BASE, USER_MMAP_END);
        if (virt == 0)
        {
            return (syscall_ret_t)MAP_FAILED; /* region exhausted */
        }
    }

    uint32_t area_prot = (uint32_t)prot & (PROT_READ | PROT_WRITE | PROT_EXEC);
    vma_kind_t kind = VMA_ANON;
    if (!(flags & MAP_ANONYMOUS))
    {
        kind = shm_is_file(entry->file) ? VMA_SHARED : VMA_DEVICE;
    }
    if (kerr_failed(vma_insert(&proc->vmas, virt, virt + len_pages, area_prot, kind)))
    {
        return (syscall_ret_t)MAP_FAILED; /* too many areas */
    }

    // anonymous memory needs nothing now: each page is backed with a zeroed
    // frame the first time it is touched (demand paging). a device maps its
    // own memory up front.
    if (!(flags & MAP_ANONYMOUS))
    {
        int32_t err = file_mmap(entry->file, proc->page_directory, virt, len_pages, prot, offset);
        if (err != 0)
        {
            // the pages mapped before the error go too
            mm_unmap(proc, virt, virt + len_pages);
            return (syscall_ret_t)MAP_FAILED;
        }
    }

    return (syscall_ret_t)virt;
}

/**
 * SYS_shm_create: make a shared memory object (kernel/memory/shm.h).
 * args: EBX = size in bytes (rounded up to pages), ECX = fd flags
 *       (FD_CLOEXEC).
 * returns the new fd (open read-write; map it with mmap), or -EINVAL,
 * -ENOMEM, -EMFILE.
 */
syscall_ret_t sys_shm_create(struct registers *regs)
{
    size_t size = syscall_arg(regs, 0);
    uint32_t fd_flags = (uint32_t)syscall_arg(regs, 1) & FD_CLOEXEC;
    process_t *proc = process_current();

    file_t *file;
    kerr_t err = shm_create(size, &file);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }
    int fd = fd_install(proc, file, fd_flags, 0);
    if (fd < 0)
    {
        file_unref(file);
    }
    return fd;
}

/**
 * SYS_munmap: remove mappings from the calling process.
 * args: EBX = addr (page-aligned), ECX = length.
 * every page of [addr, addr + length) that is mapped is removed; parts that
 * are not mapped are skipped. returns 0, or a negative errno.
 */
syscall_ret_t sys_munmap(struct registers *regs)
{
    uintptr_t addr = syscall_arg(regs, 0);
    size_t length = syscall_arg(regs, 1);
    process_t *proc = process_current();

    size_t len_pages = PAGE_ALIGN_UP(length);
    if (!proc || length == 0 || len_pages == 0 || addr + len_pages < addr)
    {
        return -EINVAL;
    }

    return kerr_to_errno(mm_unmap(proc, addr, addr + len_pages));
}

/**
 * SYS_mprotect: change the protection of mapped memory.
 * args: EBX = addr (page-aligned), ECX = length, EDX = prot.
 * returns 0, -ENOMEM if part of the range is not mapped, or another
 * negative errno.
 */
syscall_ret_t sys_mprotect(struct registers *regs)
{
    uintptr_t addr = syscall_arg(regs, 0);
    size_t length = syscall_arg(regs, 1);
    uint32_t prot = (uint32_t)syscall_arg(regs, 2) & (PROT_READ | PROT_WRITE | PROT_EXEC);
    process_t *proc = process_current();

    size_t len_pages = PAGE_ALIGN_UP(length);
    if (!proc || length == 0 || len_pages == 0 || addr + len_pages < addr)
    {
        return -EINVAL;
    }

    kerr_t err = mm_protect(proc, addr, addr + len_pages, prot);
    if (err == KERR_NOENT)
    {
        return -ENOMEM; // POSIX's error for an unmapped range
    }
    return kerr_to_errno(err);
}

/**
 * the process a pid names for setpgid and friends: 0 is the caller.
 */
static process_t *target_process(int32_t pid)
{
    if (pid < 0)
        return NULL;
    return pid == 0 ? process_current() : process_get((uint32_t)pid);
}

/**
 * SYS_setpgid: move a process into a process group.
 * args: EBX = pid (0: the caller; else the caller or one of its children),
 *       ECX = pgid (0: the target's own pid, a new group).
 * the group must be new or exist in the caller's session.
 * returns 0, or -ESRCH, -EPERM, -EINVAL.
 */
syscall_ret_t sys_setpgid(struct registers *regs)
{
    int32_t pid = (int32_t)syscall_arg(regs, 0);
    int32_t pgid = (int32_t)syscall_arg(regs, 1);
    process_t *caller = process_current();

    if (pgid < 0)
        return -EINVAL;

    process_t *target = target_process(pid);
    if (!target || (target != caller && target->parent_pid != caller->pid) ||
        target->state == PROC_STATE_ZOMBIE)
        return -ESRCH;
    if (target->sid != caller->sid || target->sid == target->pid)
        return -EPERM; // another session, or a session leader

    uint32_t group = pgid == 0 ? target->pid : (uint32_t)pgid;
    if (group != target->pid && !process_group_exists(group, caller->sid))
        return -EPERM;

    target->pgid = group;
    return 0;
}

/**
 * SYS_getpgid: the process group of a process (pid 0: the caller).
 */
syscall_ret_t sys_getpgid(struct registers *regs)
{
    process_t *target = target_process((int32_t)syscall_arg(regs, 0));
    return target ? (syscall_ret_t)target->pgid : -ESRCH;
}

/**
 * SYS_setsid: start a new session, with the caller as the leader of it and
 * of a new process group, and no controlling terminal.
 * returns the new session id, or -EPERM if the caller leads a group.
 */
syscall_ret_t sys_setsid(struct registers *regs)
{
    (void)regs;
    process_t *caller = process_current();

    if (caller->pgid == caller->pid || process_group_exists(caller->pid, caller->sid))
        return -EPERM;

    caller->sid = caller->pid;
    caller->pgid = caller->pid;
    return (syscall_ret_t)caller->sid;
}

/**
 * SYS_getsid: the session of a process (pid 0: the caller).
 */
syscall_ret_t sys_getsid(struct registers *regs)
{
    process_t *target = target_process((int32_t)syscall_arg(regs, 0));
    return target ? (syscall_ret_t)target->sid : -ESRCH;
}

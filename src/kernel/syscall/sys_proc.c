#include "syscall_internal.h"
#include "syscall.h"

#include "kernel/vfs/vfs.h"
#include "kernel/usermode/usermode.h"
#include "kernel/process/process.h"
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

// most arguments, and most argument bytes (NULs included), sys_exec takes
#define EXEC_ARGC_MAX 32
#define EXEC_ARG_BYTES 1024

/**
 * copy a user argv array and its strings into kernel memory.
 * @param user_argv the user's NULL-terminated array (may itself be NULL).
 * @param kargv receives EXEC_ARGC_MAX + 1 pointers into strings.
 * @param strings receives the strings, packed.
 * @return argc, or a negative errno (EFAULT for bad memory, EINVAL when
 *         over EXEC_ARGC_MAX or EXEC_ARG_BYTES).
 */
static int32_t copy_argv_from_user(const char **user_argv, const char **kargv, char *strings)
{
    uint32_t argc = 0;
    uint32_t used = 0;

    if (user_argv)
    {
        for (;;)
        {
            const char *arg;
            if (kerr_failed(copy_from_user(&arg, &user_argv[argc], sizeof(arg))))
                return -18; /* EFAULT */
            if (!arg)
                break;
            if (argc == EXEC_ARGC_MAX)
                return -3; /* EINVAL: too many arguments */

            int32_t len = strncpy_from_user(strings + used, arg, EXEC_ARG_BYTES - used);
            if (len == KERR_FAULT)
                return -18; /* EFAULT */
            if (len < 0)
                return -3; /* EINVAL: arguments too long */

            kargv[argc++] = strings + used;
            used += (uint32_t)len + 1;
        }
    }

    kargv[argc] = NULL;
    return (int32_t)argc;
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
    char *strings = kmalloc(EXEC_ARG_BYTES);
    if (!strings)
        return -4; /* ENOMEM */

    const char *kargv[EXEC_ARGC_MAX + 1];
    int32_t ret = copy_argv_from_user(argv, kargv, strings);
    if (ret >= 0)
    {
        ret = (int32_t)exec_program(resolved, kargv);
        if (ret < 0)
            ret = -5; /* ENOENT */
    }

    kfree(strings);
    return ret;
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
        return -4; /* ENOMEM */
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
 * SYS_waitpid: wait for a child process to exit.
 * args: EBX = child PID to wait for.
 * returns the child's exit code on success, or negative errno on error.
 *
 * if the child is already a zombie, reaps it immediately.
 * otherwise, blocks the calling process until the child exits.
 */
syscall_ret_t sys_waitpid(struct registers *regs)
{
    uint32_t child_pid = (uint32_t)syscall_arg(regs, 0);
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

    /* mask interrupts from the zombie check until we are blocked, so the
     * child cannot exit in between (it would see us not yet BLOCKED, skip
     * the wakeup, and we would sleep forever). */
    uint32_t flags = arch_irq_save();

    /* child is still running: block the parent until it exits */
    if (child->state != PROC_STATE_ZOMBIE)
    {
        parent->state = PROC_STATE_BLOCKED;
        parent->wait_for_pid = child_pid;
        scheduler_remove(parent);
        scheduler_yield();

        /* woken up: the child should now be a zombie */
        child = process_get(child_pid);
    }

    int32_t result = -19; /* ESRCH: child disappeared somehow */
    if (child && child->state == PROC_STATE_ZOMBIE)
    {
        result = child->exit_code;
        process_destroy(child);
    }

    arch_irq_restore(flags);
    return result;
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
        if (!entry || entry->type != FD_TYPE_FILE)
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
    vma_kind_t kind = (flags & MAP_ANONYMOUS) ? VMA_ANON : VMA_DEVICE;
    if (kerr_failed(vma_insert(&proc->vmas, virt, virt + len_pages, area_prot, kind)))
    {
        return (syscall_ret_t)MAP_FAILED; /* too many areas */
    }

    // anonymous memory needs nothing now: each page is backed with a zeroed
    // frame the first time it is touched (demand paging). a device maps its
    // own memory up front.
    if (!(flags & MAP_ANONYMOUS))
    {
        int32_t err = vfs_mmap(&entry->file, proc->page_directory, virt, len_pages, prot, offset);
        if (err != 0)
        {
            vma_remove(&proc->vmas, virt, virt + len_pages);
            return (syscall_ret_t)MAP_FAILED;
        }
    }

    return (syscall_ret_t)virt;
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
        return -3; /* EINVAL */
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
        return -3; /* EINVAL */
    }

    kerr_t err = mm_protect(proc, addr, addr + len_pages, prot);
    if (err == KERR_NOENT)
    {
        return -4; /* ENOMEM: POSIX's error for an unmapped range */
    }
    return kerr_to_errno(err);
}

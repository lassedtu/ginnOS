#include "syscall_internal.h"
#include "syscall.h"

#include "kernel/vfs/vfs.h"
#include "kernel/usermode/usermode.h"
#include "kernel/process/process.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pmm_layout.h"
#include "kernel/memory/mman.h"
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
 * for positive increments, allocates physical pages and maps them as
 * user-accessible for any new pages between the old and new break.
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

    /* allocate and map any new pages between old_brk and new_brk */
    uintptr_t page = PAGE_ALIGN_DOWN(old_brk);
    uintptr_t end = PAGE_ALIGN_UP(new_brk);
    process_t *proc = process_current();
    addr_space_t pd = proc ? proc->page_directory : arch_kernel_address_space();

    for (; page < end; page += PAGE_SIZE)
    {
        void *frame = pmm_alloc_page();
        if (!frame)
        {
            return -1;
        }

        memset(frame, 0, PAGE_SIZE);
        arch_map_page(pd, page, virt_to_phys(frame), MMU_USER_RW);
    }

    usermode_set_brk(new_brk);
    return (syscall_ret_t)old_brk;
}

/**
 * SYS_mmap: map memory into the calling process.
 * args: EBX = length, ECX = prot, EDX = flags, ESI = fd, EDI = offset.
 * (POSIX's advisory addr is dropped; the kernel always picks the range.)
 *
 * two forms:
 *   - anonymous (MAP_ANONYMOUS, fd < 0): allocate zeroed physical pages.
 *   - device-backed (fd is an open device node): route through vfs_mmap so the
 *     device maps its own memory (e.g. the framebuffer's LFB).
 * ranges come from the per-process mmap bump region. eager, no demand paging.
 * returns the mapped address, or MAP_FAILED (-1) on error.
 */
syscall_ret_t sys_mmap(struct registers *regs)
{
    size_t length = syscall_arg(regs, 0);
    int prot = (int)syscall_arg(regs, 1);
    int flags = (int)syscall_arg(regs, 2);
    int32_t fd = (int32_t)syscall_arg(regs, 3);
    uint32_t offset = (uint32_t)syscall_arg(regs, 4);

    process_t *proc = process_current();
    if (!proc || length == 0)
    {
        return (syscall_ret_t)MAP_FAILED;
    }

    // offset must be page-aligned (file/device mappings start on a page).
    if (!PAGE_IS_ALIGNED(offset))
    {
        return (syscall_ret_t)MAP_FAILED;
    }

    // reserve a page-aligned range from the process's mmap bump cursor.
    size_t len_pages = PAGE_ALIGN_UP(length);
    uintptr_t virt = proc->mmap_next;
    if (virt < USER_MMAP_BASE || len_pages > USER_MMAP_END - virt)
    {
        return (syscall_ret_t)MAP_FAILED; /* region exhausted */
    }

    if (flags & MAP_ANONYMOUS)
    {
        // anonymous: back each page with a fresh zeroed frame, user-accessible.
        size_t mapped = 0;
        for (; mapped < len_pages; mapped += PAGE_SIZE)
        {
            void *frame = pmm_alloc_page();
            if (!frame)
            {
                return (syscall_ret_t)MAP_FAILED;
            }
            memset(frame, 0, PAGE_SIZE);

            uint32_t page_flags = (prot & PROT_WRITE)
                                      ? MMU_USER_RW
                                      : (MMU_FLAG_PRESENT | MMU_FLAG_USER);
            arch_map_page(proc->page_directory, virt + mapped, virt_to_phys(frame), page_flags);
        }
    }
    else
    {
        // device-backed: the fd must be an open device node; dispatch to it.
        if (fd < 0)
        {
            return (syscall_ret_t)MAP_FAILED;
        }

        fd_entry_t *entry = fd_get(fd);
        if (!entry || entry->type != FD_TYPE_FILE)
        {
            return (syscall_ret_t)MAP_FAILED;
        }

        int32_t err = vfs_mmap(&entry->file, proc->page_directory, virt, len_pages, prot, offset);
        if (err != 0)
        {
            return (syscall_ret_t)MAP_FAILED;
        }
    }

    proc->mmap_next = virt + len_pages;
    return (syscall_ret_t)virt;
}

#include "syscall_internal.h"
#include "syscall.h"

#include "kernel/vfs/vfs.h"
#include "kernel/process/process.h"
#include "common/string.h"

/**
 * SYS_getcwd: copy the current working directory into a user buffer.
 * args: EBX = buffer pointer, ECX = buffer size.
 * returns 0 on success, negative errno on failure.
 */
int32_t sys_getcwd(struct registers *regs)
{
    char *buf = (char *)regs->ebx;
    uint32_t size = regs->ecx;
    process_t *proc = process_current();

    if (!is_user_ptr(buf, size))
        return -18; /* EFAULT */

    if (!proc || size == 0)
    {
        return -3; /* EINVAL */
    }

    uint32_t len = strlen(proc->cwd);
    if (len + 1 > size)
    {
        return -17; /* ERANGE */
    }

    strcpy(buf, proc->cwd);
    return 0;
}

/**
 * SYS_chdir: change the current working directory.
 * args: EBX = path string pointer.
 * returns 0 on success, negative errno on failure.
 */
int32_t sys_chdir(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;
    process_t *proc = process_current();

    if (!is_user_str(path))
        return -18; /* EFAULT */

    if (!proc)
    {
        return -3; /* EINVAL */
    }

    /* resolve the path relative to the current cwd */
    char resolved[PATH_MAX];
    if (!vfs_resolve_path(proc->cwd, path, resolved, sizeof(resolved)))
    {
        return -3; /* EINVAL */
    }

    /* verify the target is a valid directory */
    vfs_stat_t stat;
    kerr_t err = vfs_stat(resolved, &stat);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    if (stat.file_type != FS_TYPE_DIR)
    {
        return -11; /* ENOTDIR */
    }

    strncpy(proc->cwd, resolved, PATH_MAX - 1);
    proc->cwd[PATH_MAX - 1] = '\0';
    return 0;
}

/**
 * SYS_ttyctl: switch terminal mode for the calling process.
 * args: EBX = mode (0 = cooked/line-buffered, 1 = raw/event-based).
 * returns the previous mode on success, negative errno on error.
 */
int32_t sys_ttyctl(struct registers *regs)
{
    uint32_t mode = regs->ebx;
    process_t *proc = process_current();

    if (!proc)
        return -3; /* EINVAL */

    if (mode > 1)
        return -3; /* EINVAL */

    int32_t prev = (int32_t)proc->tty_raw;
    proc->tty_raw = (uint8_t)mode;
    return prev;
}

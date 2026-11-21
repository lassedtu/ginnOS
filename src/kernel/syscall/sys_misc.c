#include "syscall_internal.h"
#include "syscall.h"

#include "kernel/vfs/vfs.h"
#include "kernel/process/process.h"
#include "common/string.h"
#include "fd_table.h"
#include "kernel/vfs/permission.h"

/**
 * SYS_getcwd: copy the current working directory into a user buffer.
 * args: EBX = buffer pointer, ECX = buffer size.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_getcwd(struct registers *regs)
{
    char *buf = (char *)syscall_arg(regs, 0);
    size_t size = syscall_arg(regs, 1);
    process_t *proc = process_current();

    if (!proc || size == 0)
    {
        return -EINVAL;
    }

    size_t len = strlen(proc->cwd);
    if (len + 1 > size)
    {
        return -ERANGE;
    }

    return kerr_to_errno(copy_to_user(buf, proc->cwd, len + 1));
}

/**
 * SYS_chdir: change the current working directory.
 * args: EBX = path string pointer.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_chdir(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);
    process_t *proc = process_current();

    if (!proc)
    {
        return -EINVAL;
    }

    /* copy the path in from user memory, resolved against the cwd */
    char resolved[PATH_MAX];
    int32_t path_err = resolve_user_path(path, resolved, sizeof(resolved));
    if (path_err < 0)
        return path_err;

    /* verify the target is a valid directory */
    vfs_stat_t stat;
    kerr_t err = vfs_stat(resolved, &stat);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    if (stat.file_type != FS_TYPE_DIR)
    {
        return -ENOTDIR;
    }

    // entering a directory needs its search (execute) right
    int32_t allowed = permission_check(&stat, proc->euid, proc->egid, MAY_EXEC);
    if (allowed < 0)
    {
        return allowed;
    }

    strncpy(proc->cwd, resolved, PATH_MAX - 1);
    proc->cwd[PATH_MAX - 1] = '\0';
    return 0;
}

/**
 * SYS_ioctl: issue a device-control request on an open file descriptor.
 * args: EBX = fd, ECX = request code, EDX = argument (usually a user pointer).
 * only file-backed fds carry a filesystem that can service ioctls; devfs
 * device nodes forward to the backing device. everything else is -ENOTTY.
 * returns 0 (or a request-specific value) on success, negative errno on error.
 *
 * note: the arg pointer is request-specific, so its size is only known to the
 * device handler; this layer passes it through and the handler validates it.
 */
syscall_ret_t sys_ioctl(struct registers *regs)
{
    int32_t fd_num = (int32_t)syscall_arg(regs, 0);
    uint32_t request = (uint32_t)syscall_arg(regs, 1);
    void *arg = (void *)syscall_arg(regs, 2);

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry)
    {
        return -EBADF;
    }

    return file_ioctl(entry->file, request, arg);
}

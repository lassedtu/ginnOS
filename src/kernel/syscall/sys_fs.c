#include "syscall_internal.h"
#include "syscall.h"
#include "fd_table.h"

#include "kernel/vfs/vfs.h"
#include "kernel/process/process.h"
#include "common/memory.h"
#include "common/string.h"

/**
 * SYS_stat: get file metadata.
 * args: EBX = path string pointer, ECX = pointer to stat output struct.
 * the output struct matches vfs_stat_t (fs_stat_t).
 * returns 0 on success, negative errno on failure.
 */
int32_t sys_stat(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;
    vfs_stat_t *stat_out = (vfs_stat_t *)regs->ecx;

    if (!is_user_str(path))
        return -18; /* EFAULT */

    if (!is_user_ptr(stat_out, sizeof(vfs_stat_t)))
        return -18; /* EFAULT */

    /* resolve relative paths against the process's cwd */
    char resolved[PATH_MAX];
    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, sizeof(resolved)))
    {
        return -3; /* EINVAL */
    }

    kerr_t err = vfs_stat(resolved, stat_out);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    return 0;
}

/**
 * SYS_create: create a regular file.
 * args: EBX = path string pointer.
 * returns 0 on success, negative errno on failure.
 */
int32_t sys_create(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;

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

    kerr_t err = vfs_create(resolved);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    return 0;
}

/**
 * SYS_mkdir: create a directory.
 * args: EBX = path string pointer.
 * returns 0 on success, negative errno on failure.
 */
int32_t sys_mkdir(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;

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

    kerr_t err = vfs_mkdir(resolved);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    return 0;
}

/**
 * SYS_unlink: remove a file.
 * args: EBX = path string pointer.
 * returns 0 on success, negative errno on failure.
 */
int32_t sys_unlink(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;

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

    kerr_t err = vfs_remove(resolved);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    return 0;
}

/**
 * SYS_rmdir: remove a directory.
 * args: EBX = path string pointer.
 * returns 0 on success, negative errno on failure.
 */
int32_t sys_rmdir(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;

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

    kerr_t err = vfs_rmdir(resolved);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    return 0;
}

/**
 * SYS_readdir: read the next directory entry from an open directory fd.
 * args: EBX = fd, ECX = pointer to user dirent struct.
 *
 * user dirent layout (matches fs_dirent_t):
 *   uint32_t inode
 *   uint8_t  file_type
 *   uint32_t size
 *   char     name[256]
 *
 * returns 0 on success, negative errno on failure or end of directory.
 */
int32_t sys_readdir(struct registers *regs)
{
    int fd_num = (int)regs->ebx;
    fs_dirent_t *user_dirent = (fs_dirent_t *)regs->ecx;

    if (!is_user_ptr(user_dirent, sizeof(fs_dirent_t)))
        return -18; /* EFAULT */

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry || entry->type != FD_TYPE_FILE)
    {
        return -2; /* EBADF */
    }

    fs_dirent_t dirent;
    kerr_t err = vfs_read_entry(&entry->file, &dirent);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    /* copy to userspace */
    memcpy(user_dirent, &dirent, sizeof(fs_dirent_t));
    return 0;
}

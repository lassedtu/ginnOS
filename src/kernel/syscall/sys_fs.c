#include "syscall_internal.h"
#include "syscall.h"
#include "fd_table.h"

#include "kernel/vfs/vfs.h"
#include "kernel/process/process.h"
#include "common/memory.h"
#include "common/string.h"

int32_t resolve_user_path(const char *user_path, char *resolved, uint32_t size)
{
    char path[PATH_MAX];
    int32_t len = strncpy_from_user(path, user_path, sizeof(path));
    if (len == KERR_FAULT)
        return -EFAULT;
    if (len < 0)
        return -EINVAL; // longer than PATH_MAX

    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, size))
        return -EINVAL;

    return 0;
}

/**
 * SYS_stat: get file metadata.
 * args: EBX = path string pointer, ECX = pointer to stat output struct.
 * the output struct matches vfs_stat_t (fs_stat_t).
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_stat(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);
    vfs_stat_t *stat_out = (vfs_stat_t *)syscall_arg(regs, 1);

    /* copy the path in from user memory, resolved against the cwd */
    char resolved[PATH_MAX];
    int32_t path_err = resolve_user_path(path, resolved, sizeof(resolved));
    if (path_err < 0)
        return path_err;

    vfs_stat_t st;
    kerr_t err = vfs_stat(resolved, &st);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    return kerr_to_errno(copy_to_user(stat_out, &st, sizeof(st)));
}

/**
 * SYS_create: create a regular file.
 * args: EBX = path string pointer.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_create(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);

    /* copy the path in from user memory, resolved against the cwd */
    char resolved[PATH_MAX];
    int32_t path_err = resolve_user_path(path, resolved, sizeof(resolved));
    if (path_err < 0)
        return path_err;

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
syscall_ret_t sys_mkdir(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);

    /* copy the path in from user memory, resolved against the cwd */
    char resolved[PATH_MAX];
    int32_t path_err = resolve_user_path(path, resolved, sizeof(resolved));
    if (path_err < 0)
        return path_err;

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
syscall_ret_t sys_unlink(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);

    /* copy the path in from user memory, resolved against the cwd */
    char resolved[PATH_MAX];
    int32_t path_err = resolve_user_path(path, resolved, sizeof(resolved));
    if (path_err < 0)
        return path_err;

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
syscall_ret_t sys_rmdir(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);

    /* copy the path in from user memory, resolved against the cwd */
    char resolved[PATH_MAX];
    int32_t path_err = resolve_user_path(path, resolved, sizeof(resolved));
    if (path_err < 0)
        return path_err;

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
syscall_ret_t sys_readdir(struct registers *regs)
{
    int fd_num = (int)syscall_arg(regs, 0);
    fs_dirent_t *user_dirent = (fs_dirent_t *)syscall_arg(regs, 1);

    /* check before reading: the read advances the directory position */
    if (!user_access_ok(user_dirent, sizeof(fs_dirent_t), true))
        return -EFAULT;

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry)
    {
        return -EBADF;
    }

    fs_dirent_t dirent;
    int32_t err = file_read_entry(entry->file, &dirent);
    if (err < 0)
    {
        return err;
    }

    /* copy to userspace */
    return kerr_to_errno(copy_to_user(user_dirent, &dirent, sizeof(fs_dirent_t)));
}

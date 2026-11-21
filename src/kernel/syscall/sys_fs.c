#include "syscall_internal.h"
#include "syscall.h"
#include "fd_table.h"
#include "kernel/vfs/stat.h"
#include "kernel/vfs/permission.h"
#include "kernel/process/process.h"
#include "kernel/vfs/dirent.h"
#include "common/memory.h"
#include "common/string.h"

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

int32_t set_new_owner(const char *path, uint32_t mode)
{
    process_t *proc = process_current();
    fs_attr_t attr = {
        .what = FS_ATTR_MODE | FS_ATTR_OWNER,
        .mode = mode & ~proc->umask & 07777u,
        .uid = proc->euid,
        .gid = proc->egid,
    };

    // a filesystem without owners (devfs) keeps its own
    kerr_t err = vfs_setattr(path, &attr);
    return err == KERR_PERM ? 0 : kerr_to_errno(err);
}

/**
 * SYS_stat: get file metadata.
 * args: EBX = path string pointer, ECX = struct stat pointer (kernel/vfs/stat.h).
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_stat(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);
    struct kstat *user_stat = (struct kstat *)syscall_arg(regs, 1);

    // copy the path in from user memory, resolved against the cwd
    char resolved[PATH_MAX];
    int32_t path_err = resolve_user_path(path, resolved, sizeof(resolved));
    if (path_err < 0)
        return path_err;

    vfs_stat_t fs_stat;
    kerr_t err = vfs_stat(resolved, &fs_stat);
    if (kerr_failed(err))
        return kerr_to_errno(err);

    struct kstat st;
    stat_from_fs(&fs_stat, &st);
    return kerr_to_errno(copy_to_user(user_stat, &st, sizeof(st)));
}

/**
 * SYS_rename: rename a file or directory.
 * args: EBX = old path, ECX = new path (same filesystem).
 * returns 0 on success, negative errno on failure (EXDEV across mounts).
 */
syscall_ret_t sys_rename(struct registers *regs)
{
    char old_path[PATH_MAX];
    char new_path[PATH_MAX];

    int32_t err = resolve_user_path((const char *)syscall_arg(regs, 0), old_path,
                                    sizeof(old_path));
    if (err == 0)
        err = resolve_user_path((const char *)syscall_arg(regs, 1), new_path, sizeof(new_path));
    if (err < 0)
        return err;

    if (vfs_resolve_mount_path(old_path, NULL) != vfs_resolve_mount_path(new_path, NULL))
        return -EXDEV;

    // both directories change
    err = permission_check_parent(old_path, true);
    if (err == 0)
        err = permission_check_parent(new_path, true);
    if (err < 0)
        return err;

    return kerr_to_errno(vfs_rename(old_path, new_path));
}

// access modes (F_OK is 0: the file only has to exist)
#define ACCESS_MODE_BITS 7 // R_OK | W_OK | X_OK

/**
 * SYS_access: check that a file exists and may be accessed.
 * args: EBX = path, ECX = mode (F_OK, or R_OK | W_OK | X_OK).
 * the check uses the real user and group ids, not the effective ones (a
 * set-uid program asks what its caller may do).
 * returns 0, or a negative errno (ENOENT, EACCES).
 */
syscall_ret_t sys_access(struct registers *regs)
{
    uint32_t mode = (uint32_t)syscall_arg(regs, 1);
    if (mode & ~(uint32_t)ACCESS_MODE_BITS)
        return -EINVAL;

    char resolved[PATH_MAX];
    int32_t err = resolve_user_path((const char *)syscall_arg(regs, 0), resolved,
                                    sizeof(resolved));
    if (err < 0)
        return err;

    vfs_stat_t st;
    kerr_t found = vfs_stat(resolved, &st);
    if (kerr_failed(found))
        return kerr_to_errno(found);

    // R_OK, W_OK and X_OK are the MAY_* bits
    process_t *proc = process_current();
    return permission_check(&st, proc->uid, proc->gid, mode);
}

/**
 * SYS_mkdir: create a directory.
 * args: EBX = path string pointer, ECX = mode (taken through the umask).
 * the new directory belongs to the caller's effective user and group.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_mkdir(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);
    uint32_t mode = (uint32_t)syscall_arg(regs, 1);

    // copy the path in from user memory, resolved against the cwd
    char resolved[PATH_MAX];
    int32_t ret = resolve_user_path(path, resolved, sizeof(resolved));
    if (ret < 0)
        return ret;

    ret = permission_check_parent(resolved, false);
    if (ret < 0)
        return ret;

    kerr_t err = vfs_mkdir(resolved);
    if (kerr_failed(err))
        return kerr_to_errno(err);

    return set_new_owner(resolved, mode);
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

    int32_t allowed = permission_check_parent(resolved, true);
    if (allowed < 0)
        return allowed;

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

    int32_t allowed = permission_check_parent(resolved, true);
    if (allowed < 0)
        return allowed;

    kerr_t err = vfs_rmdir(resolved);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    return 0;
}

// the size of a record with the longest name (a name has at most
// EXT2_NAME_MAX - 1 characters and its NUL)
#define DIRENT_RECORD_MAX ((KDIRENT_HEADER + EXT2_NAME_MAX + 3) & ~3u)

/**
 * the DT_ type of a directory entry.
 */
static uint8_t dirent_type(const fs_dirent_t *entry)
{
    switch (entry->file_type)
    {
    case FS_TYPE_DIR:
        return DT_DIR;
    case FS_TYPE_FILE:
        return DT_REG;
    default:
        return DT_UNKNOWN;
    }
}

/**
 * SYS_getdents: read directory entries.
 * args: EBX = fd of an open directory, ECX = buffer, EDX = buffer size.
 * fills the buffer with as many whole records (kernel/vfs/dirent.h) as fit.
 * returns the bytes written, 0 at the end of the directory, or a negative
 * errno (EINVAL if the buffer cannot hold the next record, ENOTDIR if fd
 * is not a directory).
 */
syscall_ret_t sys_getdents(struct registers *regs)
{
    uint8_t *user_buf = (uint8_t *)syscall_arg(regs, 1);
    uint32_t size = (uint32_t)syscall_arg(regs, 2);

    if (!user_access_ok(user_buf, size, true))
        return -EFAULT;
    fd_entry_t *fd_entry = fd_get((int)syscall_arg(regs, 0));
    if (!fd_entry)
        return -EBADF;

    // one record at a time, built here and copied out
    uint8_t record[DIRENT_RECORD_MAX];
    fs_dirent_t entry;
    uint32_t used = 0;

    for (;;)
    {
        int32_t err = file_read_entry(fd_entry->file, &entry);
        if (err == -ENOENT)
            break; // the end of the directory
        if (err < 0)
            return used > 0 ? (syscall_ret_t)used : err;

        entry.name[EXT2_NAME_MAX - 1] = '\0';
        uint32_t name_len = (uint32_t)strlen(entry.name); // < EXT2_NAME_MAX
        uint32_t reclen = (KDIRENT_HEADER + name_len + 1 + 3) & ~3u;
        if (used + reclen > size)
        {
            // keep it for the next call
            err = file_unread_entry(fd_entry->file, &entry);
            if (err < 0)
                return err;
            if (used == 0)
                return -EINVAL;
            break;
        }

        struct kdirent *out = (struct kdirent *)record;
        memset(record, 0, reclen);
        out->d_ino = entry.inode;
        out->d_reclen = (uint16_t)reclen;
        out->d_type = dirent_type(&entry);
        memcpy(out->d_name, entry.name, name_len + 1);

        if (kerr_failed(copy_to_user(user_buf + used, record, reclen)))
            return -EFAULT;
        used += reclen;
    }

    return (syscall_ret_t)used;
}

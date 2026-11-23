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
    return resolve_user_path_at(AT_FDCWD, user_path, resolved, size, true);
}

int32_t resolve_user_path_at(int32_t dirfd, const char *user_path, char *resolved, uint32_t size,
                             bool follow)
{
    char path[PATH_MAX];
    int32_t len = strncpy_from_user(path, user_path, sizeof(path));
    if (len == KERR_FAULT)
        return -EFAULT;
    if (len < 0)
        return -ENAMETOOLONG;
    if (len == 0)
        return -ENOENT;

    process_t *proc = process_current();
    const char *base = proc ? proc->cwd : "/";

    // a relative path in a directory the caller has open
    if (path[0] != '/' && dirfd != AT_FDCWD)
    {
        fd_entry_t *entry = fd_get(dirfd);
        if (!entry)
            return -EBADF;
        file_t *dir = entry->file;
        if (!dir->path)
            return -ENOTDIR; // a pipe, a socket or a device stream
        if (vfs_file_type(&dir->vfs) != FS_TYPE_DIR)
            return -ENOTDIR;
        base = dir->path;
    }

    if (!vfs_resolve_path(base, path, resolved, size))
        return -ENAMETOOLONG;

    kerr_t err = vfs_follow(resolved, size, follow);
    if (err == KERR_RANGE)
        return -ENAMETOOLONG;
    return kerr_to_errno(err);
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

/** stat a path relative to dirfd into user memory */
static int32_t stat_at(int32_t dirfd, const char *path, struct kstat *user_stat, bool follow)
{
    char resolved[PATH_MAX];
    int32_t err = resolve_user_path_at(dirfd, path, resolved, sizeof(resolved), follow);
    if (err < 0)
        return err;

    vfs_stat_t fs_stat;
    kerr_t found = vfs_stat(resolved, &fs_stat);
    if (kerr_failed(found))
        return kerr_to_errno(found);

    struct kstat st;
    stat_from_fs(&fs_stat, &st);
    return kerr_to_errno(copy_to_user(user_stat, &st, sizeof(st)));
}

/**
 * SYS_stat: get file metadata.
 * args: path, struct stat pointer (kernel/vfs/stat.h).
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_stat(struct registers *regs)
{
    return stat_at(AT_FDCWD, (const char *)syscall_arg(regs, 0),
                   (struct kstat *)syscall_arg(regs, 1), true);
}

/**
 * SYS_fstatat: stat a path relative to a directory.
 * args: dirfd, path, struct stat pointer, flags (AT_SYMLINK_NOFOLLOW).
 */
syscall_ret_t sys_fstatat(struct registers *regs)
{
    uint32_t flags = (uint32_t)syscall_arg(regs, 3);
    if (flags & ~(uint32_t)AT_SYMLINK_NOFOLLOW)
        return -EINVAL;
    return stat_at((int32_t)syscall_arg(regs, 0), (const char *)syscall_arg(regs, 1),
                   (struct kstat *)syscall_arg(regs, 2), !(flags & AT_SYMLINK_NOFOLLOW));
}

static int32_t rename_at(int32_t old_dirfd, const char *old_user, int32_t new_dirfd,
                         const char *new_user)
{
    char old_path[PATH_MAX];
    char new_path[PATH_MAX];

    int32_t err = resolve_user_path_at(old_dirfd, old_user, old_path, sizeof(old_path), false);
    if (err == 0)
        err = resolve_user_path_at(new_dirfd, new_user, new_path, sizeof(new_path), false);
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

/**
 * SYS_rename: rename a file or directory.
 * args: old path, new path (same filesystem).
 * returns 0 on success, negative errno on failure (EXDEV across mounts).
 */
syscall_ret_t sys_rename(struct registers *regs)
{
    return rename_at(AT_FDCWD, (const char *)syscall_arg(regs, 0), AT_FDCWD,
                     (const char *)syscall_arg(regs, 1));
}

/**
 * SYS_renameat: rename, each path relative to its own directory.
 * args: old dirfd, old path, new dirfd, new path.
 */
syscall_ret_t sys_renameat(struct registers *regs)
{
    return rename_at((int32_t)syscall_arg(regs, 0), (const char *)syscall_arg(regs, 1),
                     (int32_t)syscall_arg(regs, 2), (const char *)syscall_arg(regs, 3));
}

// access modes (F_OK is 0: the file only has to exist)
#define ACCESS_MODE_BITS 7 // R_OK | W_OK | X_OK

/**
 * check that a file exists and may be accessed: with the real user and
 * group ids (a set-uid program asks what its caller may do), or with the
 * effective ones (AT_EACCESS).
 */
static int32_t access_at(int32_t dirfd, const char *path, uint32_t mode, uint32_t flags)
{
    if (mode & ~(uint32_t)ACCESS_MODE_BITS)
        return -EINVAL;
    if (flags & ~(uint32_t)(AT_EACCESS | AT_SYMLINK_NOFOLLOW))
        return -EINVAL;

    char resolved[PATH_MAX];
    int32_t err = resolve_user_path_at(dirfd, path, resolved, sizeof(resolved),
                                       !(flags & AT_SYMLINK_NOFOLLOW));
    if (err < 0)
        return err;

    vfs_stat_t st;
    kerr_t found = vfs_stat(resolved, &st);
    if (kerr_failed(found))
        return kerr_to_errno(found);

    // R_OK, W_OK and X_OK are the MAY_* bits
    process_t *proc = process_current();
    if (flags & AT_EACCESS)
        return permission_check(&st, proc->euid, proc->egid, mode);
    return permission_check(&st, proc->uid, proc->gid, mode);
}

/**
 * SYS_access: check that a file exists and may be accessed.
 * args: path, mode (F_OK, or R_OK | W_OK | X_OK).
 * returns 0, or a negative errno (ENOENT, EACCES).
 */
syscall_ret_t sys_access(struct registers *regs)
{
    return access_at(AT_FDCWD, (const char *)syscall_arg(regs, 0),
                     (uint32_t)syscall_arg(regs, 1), 0);
}

/**
 * SYS_faccessat: access, relative to a directory.
 * args: dirfd, path, mode, flags (AT_EACCESS).
 */
syscall_ret_t sys_faccessat(struct registers *regs)
{
    return access_at((int32_t)syscall_arg(regs, 0), (const char *)syscall_arg(regs, 1),
                     (uint32_t)syscall_arg(regs, 2), (uint32_t)syscall_arg(regs, 3));
}

/**
 * create a directory. the new directory belongs to the caller's effective
 * user and group; its mode is taken through the umask.
 */
static int32_t mkdir_at(int32_t dirfd, const char *path, uint32_t mode)
{
    char resolved[PATH_MAX];
    int32_t ret = resolve_user_path_at(dirfd, path, resolved, sizeof(resolved), false);
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
 * SYS_mkdir: create a directory.
 * args: path, mode.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_mkdir(struct registers *regs)
{
    return mkdir_at(AT_FDCWD, (const char *)syscall_arg(regs, 0),
                    (uint32_t)syscall_arg(regs, 1));
}

/**
 * SYS_mkdirat: mkdir relative to a directory.
 * args: dirfd, path, mode.
 */
syscall_ret_t sys_mkdirat(struct registers *regs)
{
    return mkdir_at((int32_t)syscall_arg(regs, 0), (const char *)syscall_arg(regs, 1),
                    (uint32_t)syscall_arg(regs, 2));
}

/** remove a file, or a directory (AT_REMOVEDIR) */
static int32_t unlink_at(int32_t dirfd, const char *path, uint32_t flags)
{
    if (flags & ~(uint32_t)AT_REMOVEDIR)
        return -EINVAL;

    char resolved[PATH_MAX];
    int32_t err = resolve_user_path_at(dirfd, path, resolved, sizeof(resolved), false);
    if (err < 0)
        return err;

    err = permission_check_parent(resolved, true);
    if (err < 0)
        return err;

    kerr_t removed = flags & AT_REMOVEDIR ? vfs_rmdir(resolved) : vfs_remove(resolved);
    return kerr_to_errno(removed);
}

/**
 * SYS_unlink: remove a file.
 * args: path.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_unlink(struct registers *regs)
{
    return unlink_at(AT_FDCWD, (const char *)syscall_arg(regs, 0), 0);
}

/**
 * SYS_rmdir: remove a directory.
 * args: path.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_rmdir(struct registers *regs)
{
    return unlink_at(AT_FDCWD, (const char *)syscall_arg(regs, 0), AT_REMOVEDIR);
}

/**
 * SYS_unlinkat: remove a file or (AT_REMOVEDIR) a directory, relative to a
 * directory.
 * args: dirfd, path, flags.
 */
syscall_ret_t sys_unlinkat(struct registers *regs)
{
    return unlink_at((int32_t)syscall_arg(regs, 0), (const char *)syscall_arg(regs, 1),
                     (uint32_t)syscall_arg(regs, 2));
}

/**
 * SYS_symlinkat: make a symbolic link.
 * args: target (the path the link points at; it need not exist), dirfd,
 *       path of the new link.
 * the link belongs to the caller's effective user and group.
 * returns 0, or a negative errno (EEXIST, ENOENT, EACCES, ENAMETOOLONG).
 */
syscall_ret_t sys_symlinkat(struct registers *regs)
{
    char target[PATH_MAX];
    int32_t length = strncpy_from_user(target, (const char *)syscall_arg(regs, 0), sizeof(target));
    if (length == KERR_FAULT)
        return -EFAULT;
    if (length < 0)
        return -ENAMETOOLONG;
    if (length == 0)
        return -ENOENT;

    char resolved[PATH_MAX];
    int32_t err = resolve_user_path_at((int32_t)syscall_arg(regs, 1),
                                       (const char *)syscall_arg(regs, 2), resolved,
                                       sizeof(resolved), false);
    if (err == 0)
        err = permission_check_parent(resolved, false);
    if (err < 0)
        return err;

    kerr_t made = vfs_symlink(resolved, target);
    if (made == KERR_RANGE)
        return -ENAMETOOLONG;
    if (kerr_failed(made))
        return kerr_to_errno(made);

    // the owner only: a link's mode is always 0777
    process_t *proc = process_current();
    fs_attr_t attr = {.what = FS_ATTR_OWNER, .uid = proc->euid, .gid = proc->egid};
    kerr_t owned = vfs_setattr(resolved, &attr);
    return owned == KERR_PERM ? 0 : kerr_to_errno(owned);
}

/**
 * SYS_readlinkat: read the target of a symbolic link.
 * args: dirfd, path, buffer, buffer size.
 * returns the bytes put in the buffer (no NUL; a longer target is cut),
 * or a negative errno (EINVAL: not a link).
 */
syscall_ret_t sys_readlinkat(struct registers *regs)
{
    char *user_buffer = (char *)syscall_arg(regs, 2);
    uint32_t size = (uint32_t)syscall_arg(regs, 3);
    if (size == 0)
        return -EINVAL;

    char resolved[PATH_MAX];
    int32_t err = resolve_user_path_at((int32_t)syscall_arg(regs, 0),
                                       (const char *)syscall_arg(regs, 1), resolved,
                                       sizeof(resolved), false);
    if (err < 0)
        return err;

    char target[PATH_MAX];
    kerr_t found = vfs_readlink(resolved, target, sizeof(target));
    if (kerr_failed(found))
        return kerr_to_errno(found);

    uint32_t length = (uint32_t)strlen(target);
    if (length > size)
        length = size;
    kerr_t copied = copy_to_user(user_buffer, target, length);
    return kerr_failed(copied) ? kerr_to_errno(copied) : (syscall_ret_t)length;
}

/**
 * SYS_linkat: give a file one more name (a hard link).
 * args: old dirfd, old path, new dirfd, new path, flags (AT_SYMLINK_FOLLOW:
 *       link to what a symbolic link points at, not the link itself).
 * returns 0, or a negative errno (EXDEV across mounts, EPERM for a
 * directory, EEXIST, EMLINK).
 */
syscall_ret_t sys_linkat(struct registers *regs)
{
    uint32_t flags = (uint32_t)syscall_arg(regs, 4);
    if (flags & ~(uint32_t)AT_SYMLINK_FOLLOW)
        return -EINVAL;

    char old_path[PATH_MAX];
    char new_path[PATH_MAX];
    int32_t err = resolve_user_path_at((int32_t)syscall_arg(regs, 0),
                                       (const char *)syscall_arg(regs, 1), old_path,
                                       sizeof(old_path), (flags & AT_SYMLINK_FOLLOW) != 0);
    if (err == 0)
        err = resolve_user_path_at((int32_t)syscall_arg(regs, 2),
                                   (const char *)syscall_arg(regs, 3), new_path,
                                   sizeof(new_path), false);
    if (err < 0)
        return err;

    if (vfs_resolve_mount_path(old_path, NULL) != vfs_resolve_mount_path(new_path, NULL))
        return -EXDEV;
    err = permission_check_parent(new_path, false);
    if (err < 0)
        return err;

    // KERR_PERM: a filesystem with no hard links (tmpfs)
    kerr_t linked = vfs_link(old_path, new_path);
    if (linked == KERR_ISDIR || linked == KERR_PERM)
        return -EPERM;
    if (linked == KERR_RANGE)
        return -EMLINK;
    return kerr_to_errno(linked);
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
    case FS_TYPE_SYMLINK:
        return DT_LNK;
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

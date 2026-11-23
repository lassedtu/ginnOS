/**
 * @file sys_cred.c
 * @brief user and group ids, file modes and owners.
 *
 * a process has a real, an effective and a saved user id (and the same
 * for groups). the effective ids decide what it may do; root (effective
 * uid 0) may do everything. a set-uid program starts with the effective
 * and saved ids of the file's owner (exec.c).
 */

#include "syscall_internal.h"
#include "fd_table.h"

#include "kernel/process/process.h"
#include "kernel/vfs/permission.h"
#include "kernel/vfs/vfs.h"

syscall_ret_t sys_getuid(struct registers *regs)
{
    (void)regs;
    return (syscall_ret_t)process_current()->uid;
}

syscall_ret_t sys_geteuid(struct registers *regs)
{
    (void)regs;
    return (syscall_ret_t)process_current()->euid;
}

syscall_ret_t sys_getgid(struct registers *regs)
{
    (void)regs;
    return (syscall_ret_t)process_current()->gid;
}

syscall_ret_t sys_getegid(struct registers *regs)
{
    (void)regs;
    return (syscall_ret_t)process_current()->egid;
}

/**
 * set a real/effective/saved id triple as setuid and setgid do: root sets
 * all three; anyone else may set the effective id to the real or saved one.
 */
static int32_t set_ids(uint32_t *real, uint32_t *effective, uint32_t *saved, uint32_t id)
{
    if (process_current()->euid == 0)
    {
        *real = *effective = *saved = id;
        return 0;
    }
    if (id == *real || id == *saved)
    {
        *effective = id;
        return 0;
    }
    return -EPERM;
}

/**
 * set only the effective id (seteuid, setegid).
 */
static int32_t set_effective(const uint32_t *real, uint32_t *effective, const uint32_t *saved,
                             uint32_t id)
{
    if (process_current()->euid == 0 || id == *real || id == *saved)
    {
        *effective = id;
        return 0;
    }
    return -EPERM;
}

/**
 * SYS_setuid: args: EBX = uid. returns 0 or -EPERM.
 */
syscall_ret_t sys_setuid(struct registers *regs)
{
    process_t *proc = process_current();
    return set_ids(&proc->uid, &proc->euid, &proc->suid, (uint32_t)syscall_arg(regs, 0));
}

/**
 * SYS_setgid: args: EBX = gid. returns 0 or -EPERM. root's check is on the
 * effective user id, so set the group before giving up root.
 */
syscall_ret_t sys_setgid(struct registers *regs)
{
    process_t *proc = process_current();
    return set_ids(&proc->gid, &proc->egid, &proc->sgid, (uint32_t)syscall_arg(regs, 0));
}

syscall_ret_t sys_seteuid(struct registers *regs)
{
    process_t *proc = process_current();
    return set_effective(&proc->uid, &proc->euid, &proc->suid, (uint32_t)syscall_arg(regs, 0));
}

syscall_ret_t sys_setegid(struct registers *regs)
{
    process_t *proc = process_current();
    return set_effective(&proc->gid, &proc->egid, &proc->sgid, (uint32_t)syscall_arg(regs, 0));
}

/**
 * the caller may change a file's mode if it owns it (or is root).
 */
static int32_t may_change_mode(const fs_stat_t *st)
{
    process_t *proc = process_current();
    return proc->euid == 0 || proc->euid == st->uid ? 0 : -EPERM;
}

static int32_t chmod_at(int32_t dirfd, const char *path, uint32_t mode)
{
    char resolved[PATH_MAX];
    int32_t ret = resolve_user_path_at(dirfd, path, resolved, sizeof(resolved));
    if (ret < 0)
        return ret;

    vfs_stat_t st;
    kerr_t err = vfs_stat(resolved, &st);
    if (kerr_failed(err))
        return kerr_to_errno(err);
    ret = may_change_mode(&st);
    if (ret < 0)
        return ret;

    fs_attr_t attr = {.what = FS_ATTR_MODE, .mode = mode & 07777u};
    return kerr_to_errno(vfs_setattr(resolved, &attr));
}

/**
 * SYS_chmod: args: path, mode (07777 bits).
 * only the owner or root. returns 0 or a negative errno.
 */
syscall_ret_t sys_chmod(struct registers *regs)
{
    return chmod_at(AT_FDCWD, (const char *)syscall_arg(regs, 0), (uint32_t)syscall_arg(regs, 1));
}

/**
 * SYS_fchmodat: args: dirfd, path, mode, flags (AT_SYMLINK_NOFOLLOW is
 * refused: a link has no mode of its own, as on Linux).
 */
syscall_ret_t sys_fchmodat(struct registers *regs)
{
    if ((uint32_t)syscall_arg(regs, 3) != 0)
        return -EOPNOTSUPP;
    return chmod_at((int32_t)syscall_arg(regs, 0), (const char *)syscall_arg(regs, 1),
                    (uint32_t)syscall_arg(regs, 2));
}

/**
 * SYS_fchmod: args: EBX = fd, ECX = mode. like chmod, on an open file.
 */
syscall_ret_t sys_fchmod(struct registers *regs)
{
    fd_entry_t *entry = fd_get((int)syscall_arg(regs, 0));
    if (!entry)
        return -EBADF;
    if (!file_is_vfs(entry->file))
    {
        // pipes and the console: no inode to change
        return -EINVAL;
    }

    vfs_stat_t st;
    kerr_t err = vfs_fstat(&entry->file->vfs, &st);
    if (kerr_failed(err))
        return kerr_to_errno(err);
    int32_t ret = may_change_mode(&st);
    if (ret < 0)
        return ret;

    fs_attr_t attr = {.what = FS_ATTR_MODE, .mode = (uint32_t)syscall_arg(regs, 1) & 07777u};
    return kerr_to_errno(vfs_fsetattr(&entry->file->vfs, &attr));
}

/**
 * the change of owner the caller asks for, if it may make it: root may
 * give a file to anyone; the owner may only change its group to its own
 * effective group. a change of owner clears the set-uid and set-gid bits
 * (so a program cannot be made set-uid to someone else).
 * @param uid the new owner, or -1 to keep it (and gid the same).
 * @return 0 with *attr filled, or -EPERM.
 */
static int32_t owner_change(const vfs_stat_t *st, uint32_t uid, uint32_t gid, fs_attr_t *attr)
{
    uid = uid == UINT32_MAX ? st->uid : uid;
    gid = gid == UINT32_MAX ? st->gid : gid;

    // anyone but root: only the owner, only the group, only to its own
    process_t *proc = process_current();
    if (proc->euid != 0 &&
        (st->uid != proc->euid || uid != st->uid || (gid != st->gid && gid != proc->egid)))
    {
        return -EPERM;
    }

    *attr = (fs_attr_t){.what = FS_ATTR_OWNER, .uid = uid, .gid = gid};
    if (uid != st->uid || gid != st->gid)
    {
        attr->what |= FS_ATTR_MODE;
        attr->mode = st->mode & 07777u & ~(uint32_t)(S_ISUID | S_ISGID);
    }
    return 0;
}

static int32_t chown_at(int32_t dirfd, const char *path, uint32_t uid, uint32_t gid)
{
    char resolved[PATH_MAX];
    int32_t ret = resolve_user_path_at(dirfd, path, resolved, sizeof(resolved));
    if (ret < 0)
        return ret;

    vfs_stat_t st;
    kerr_t err = vfs_stat(resolved, &st);
    if (kerr_failed(err))
        return kerr_to_errno(err);

    fs_attr_t attr;
    ret = owner_change(&st, uid, gid, &attr);
    if (ret < 0)
        return ret;
    return kerr_to_errno(vfs_setattr(resolved, &attr));
}

/**
 * SYS_chown: args: path, uid, gid (-1 keeps one).
 * returns 0 or a negative errno (see owner_change).
 */
syscall_ret_t sys_chown(struct registers *regs)
{
    return chown_at(AT_FDCWD, (const char *)syscall_arg(regs, 0), (uint32_t)syscall_arg(regs, 1),
                    (uint32_t)syscall_arg(regs, 2));
}

/**
 * SYS_fchownat: args: dirfd, path, uid, gid, flags (AT_SYMLINK_NOFOLLOW).
 */
syscall_ret_t sys_fchownat(struct registers *regs)
{
    if ((uint32_t)syscall_arg(regs, 4) & ~(uint32_t)AT_SYMLINK_NOFOLLOW)
        return -EINVAL;
    return chown_at((int32_t)syscall_arg(regs, 0), (const char *)syscall_arg(regs, 1),
                    (uint32_t)syscall_arg(regs, 2), (uint32_t)syscall_arg(regs, 3));
}

/**
 * SYS_fchown: args: fd, uid, gid. like chown, on an open file.
 */
syscall_ret_t sys_fchown(struct registers *regs)
{
    fd_entry_t *entry = fd_get((int)syscall_arg(regs, 0));
    if (!entry)
        return -EBADF;
    if (!file_is_vfs(entry->file))
        return -EINVAL;

    vfs_stat_t st;
    kerr_t err = vfs_fstat(&entry->file->vfs, &st);
    if (kerr_failed(err))
        return kerr_to_errno(err);

    fs_attr_t attr;
    int32_t ret = owner_change(&st, (uint32_t)syscall_arg(regs, 1), (uint32_t)syscall_arg(regs, 2),
                               &attr);
    if (ret < 0)
        return ret;
    return kerr_to_errno(vfs_fsetattr(&entry->file->vfs, &attr));
}

/**
 * SYS_umask: args: EBX = mask. returns the old mask.
 */
syscall_ret_t sys_umask(struct registers *regs)
{
    process_t *proc = process_current();
    uint32_t old = proc->umask;
    proc->umask = (uint32_t)syscall_arg(regs, 0) & 0777u;
    return (syscall_ret_t)old;
}

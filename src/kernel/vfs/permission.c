#include "permission.h"

#include "common/memory.h"
#include "common/string.h"
#include "kernel/errno.h"
#include "kernel/process/process.h"
#include "kernel/vfs/stat.h"
#include "kernel/vfs/vfs.h"

int32_t permission_check(const fs_stat_t *st, uint32_t uid, uint32_t gid, uint32_t want)
{
    uint32_t mode = st->mode;

    if (uid == 0)
    {
        // root: everything but running a file nobody may run
        bool runnable = (mode & 0111) != 0 || (mode & S_IFMT) == S_IFDIR;
        return (want & MAY_EXEC) && !runnable ? -EACCES : 0;
    }

    uint32_t allowed;
    if (uid == st->uid)
        allowed = (mode >> 6) & 7;
    else if (gid == st->gid)
        allowed = (mode >> 3) & 7;
    else
        allowed = mode & 7;

    return (want & ~allowed) ? -EACCES : 0;
}

int32_t permission_check_path(const char *path, uint32_t want)
{
    vfs_stat_t st;
    kerr_t err = vfs_stat(path, &st);
    if (kerr_failed(err))
        return kerr_to_errno(err);

    process_t *proc = process_current();
    return permission_check(&st, proc->euid, proc->egid, want);
}

void permission_parent_path(const char *path, char *out, uint32_t size)
{
    uint32_t len = (uint32_t)strlen(path);
    while (len > 1 && path[len - 1] == '/')
        len--;
    while (len > 0 && path[len - 1] != '/')
        len--;
    while (len > 1 && path[len - 1] == '/')
        len--;
    if (len == 0)
        len = 1; // the root
    if (len >= size)
        len = size - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

int32_t permission_check_parent(const char *path, bool victim)
{
    char parent[VFS_PATH_MAX];
    permission_parent_path(path, parent, sizeof(parent));

    vfs_stat_t dir;
    kerr_t err = vfs_stat(parent, &dir);
    if (kerr_failed(err))
        return kerr_to_errno(err);

    process_t *proc = process_current();
    int32_t result = permission_check(&dir, proc->euid, proc->egid, MAY_WRITE | MAY_EXEC);
    if (result < 0 || !victim || proc->euid == 0 || !(dir.mode & S_ISVTX))
        return result;

    // a sticky directory: only the entry's owner or the directory's
    vfs_stat_t entry;
    if (kerr_ok(vfs_stat(path, &entry)) && entry.uid != proc->euid && dir.uid != proc->euid)
        return -EPERM;
    return 0;
}

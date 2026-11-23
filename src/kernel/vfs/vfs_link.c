#include "vfs.h"

#include "common/memory.h"
#include "common/string.h"

/**
 * @file vfs_link.c
 * @brief symbolic links across the mounts, and hard links.
 *
 * the filesystems resolve plain paths only. before an operation, the
 * syscall layer has vfs_follow replace each symbolic link along the path by
 * its target, so the path that reaches the filesystem has no links (but
 * the last name, when the operation acts on a link itself). a target can
 * point into another mount, so each step finds the mount again.
 *
 * the path is made canonical ("." and ".." removed) before the links are
 * followed, so "link/.." is the directory that holds link, as in a shell's
 * logical mode.
 */

// links followed for one path before ELOOP, as Linux
#define FOLLOW_MAX 40

kerr_t vfs_follow(char *path, uint32_t size, bool follow_last)
{
    char target[VFS_PATH_MAX];
    char next[VFS_PATH_MAX];

    for (int links = 0;; links++)
    {
        const char *rel = NULL;
        fs_mount_t *mount = vfs_resolve_mount_path(path, &rel);
        if (!mount)
        {
            return KERR_OK; // the operation reports it
        }

        uint32_t end = 0;
        kerr_t err = fs_find_link(mount, rel, follow_last, target, sizeof(target), &end);
        if (kerr_failed(err))
        {
            return err;
        }
        if (end == 0)
        {
            return KERR_OK;
        }
        if (links == FOLLOW_MAX)
        {
            return KERR_LOOP;
        }

        // rel points into path (a name was found, so it is not the static "/")
        uint32_t link_end = (uint32_t)(rel - path) + end;
        const char *rest = path + link_end;
        size_t length;
        if (target[0] == '/')
        {
            length = strlen(target) + strlen(rest);
            if (length >= sizeof(next))
            {
                return KERR_RANGE;
            }
            strcpy(next, target);
            strcpy(next + strlen(target), rest);
        }
        else
        {
            // relative: from the directory that holds the link
            uint32_t dir_end = link_end;
            while (dir_end > 0 && path[dir_end - 1] != '/')
            {
                dir_end--;
            }
            length = dir_end + strlen(target) + strlen(rest);
            if (length >= sizeof(next))
            {
                return KERR_RANGE;
            }
            memcpy(next, path, dir_end);
            strcpy(next + dir_end, target);
            strcpy(next + dir_end + strlen(target), rest);
        }

        if (!vfs_resolve_path("/", next, path, size))
        {
            return KERR_RANGE;
        }
    }
}

kerr_t vfs_readlink(const char *path, char *target, uint32_t size)
{
    const char *rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(path, &rel);
    if (!mount)
    {
        return KERR_INVAL;
    }
    return fs_readlink(mount, rel, target, size);
}

kerr_t vfs_symlink(const char *path, const char *target)
{
    const char *rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(path, &rel);
    if (!mount)
    {
        return KERR_INVAL;
    }
    return fs_symlink(mount, rel, target);
}

kerr_t vfs_link(const char *old_path, const char *new_path)
{
    const char *old_rel = NULL;
    const char *new_rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(old_path, &old_rel);
    if (!mount || vfs_resolve_mount_path(new_path, &new_rel) != mount)
    {
        return KERR_INVAL;
    }
    return fs_link(mount, old_rel, new_rel);
}

kerr_t vfs_mknod(const char *path, uint32_t mode, uint32_t rdev)
{
    const char *rel = NULL;
    fs_mount_t *mount = vfs_resolve_mount_path(path, &rel);
    if (!mount)
    {
        return KERR_INVAL;
    }
    return fs_mknod(mount, rel, mode, rdev);
}

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
 *
 * a process can have a root directory of its own (chroot). its paths are
 * then "virtual": "/" is that directory. an absolute link target is taken
 * in the root too, and ".." stops at it.
 */

// links followed for one path before ELOOP, as Linux
#define FOLLOW_MAX 40

const char *vfs_inside_root(const char *path, const char *root)
{
    size_t length = strlen(root);
    if (length <= 1)
    {
        return path; // the real root
    }
    if (strncmp(path, root, length) != 0 || (path[length] != '/' && path[length] != '\0'))
    {
        return path; // outside the root (a working directory from before)
    }
    return path[length] == '\0' ? "/" : path + length;
}

bool vfs_join_root(const char *root, const char *virtual_path, char *out, uint32_t size)
{
    size_t root_length = strlen(root);
    if (root_length <= 1)
    {
        root_length = 0; // the real root adds nothing
    }
    const char *rest = strcmp(virtual_path, "/") == 0 && root_length ? "" : virtual_path;
    size_t rest_length = strlen(rest);
    if (root_length + rest_length + 1 > size)
    {
        return false;
    }
    memcpy(out, root, root_length);
    memcpy(out + root_length, rest, rest_length + 1);
    return true;
}

kerr_t vfs_follow(char *path, uint32_t size, bool follow_last, const char *root)
{
    char target[VFS_PATH_MAX];
    char next[VFS_PATH_MAX];
    char canonical[VFS_PATH_MAX];

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

        // rel points into path (a name was found, so it is not the static "/").
        // the new path is made in the root's terms ("virtual"), so that an
        // absolute target and ".." stay in the root
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
            char saved = path[dir_end];
            path[dir_end] = '\0';
            const char *dir = vfs_inside_root(path, root);
            size_t dir_length = strlen(dir);
            length = dir_length + 1 + strlen(target) + strlen(rest);
            if (length >= sizeof(next))
            {
                path[dir_end] = saved;
                return KERR_RANGE;
            }
            memcpy(next, dir, dir_length);
            path[dir_end] = saved;
            next[dir_length] = '/';
            strcpy(next + dir_length + 1, target);
            strcpy(next + dir_length + 1 + strlen(target), rest);
        }

        if (!vfs_resolve_path("/", next, canonical, sizeof(canonical)) ||
            !vfs_join_root(root, canonical, path, size))
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

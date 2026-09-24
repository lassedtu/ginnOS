#include "vfs.h"

#include "common/string.h"

/**
 * @file vfs_mount.c
 * @brief VFS mount table.
 *
 * holds the set of mounted filesystems keyed by path prefix. path-based
 * operations resolve a path to the mount whose prefix is the longest match,
 * so "/" (the root) catches everything not claimed by a more specific mount
 * like "/dev" or "/proc". today only root is mounted, but the table makes
 * additional pseudo-filesystems a matter of calling vfs_mount().
 */

#define VFS_MOUNT_MAX 8

typedef struct
{
    char prefix[VFS_PATH_MAX]; // mount point, e.g. "/" or "/dev"
    fs_mount_t *mount;         // backing filesystem, or NULL if the slot is free
} vfs_mount_entry_t;

static vfs_mount_entry_t mount_table[VFS_MOUNT_MAX];

/**
 * find the slot holding an exact prefix match, or NULL if none.
 */
static vfs_mount_entry_t *find_exact(const char *prefix)
{
    for (int i = 0; i < VFS_MOUNT_MAX; i++)
    {
        if (mount_table[i].mount && strcmp(mount_table[i].prefix, prefix) == 0)
        {
            return &mount_table[i];
        }
    }
    return NULL;
}

/**
 * check whether prefix covers path: either prefix is "/" (covers everything)
 * or path equals prefix or begins with "prefix/".
 */
static bool prefix_covers(const char *prefix, const char *path)
{
    if (prefix[0] == '/' && prefix[1] == '\0')
    {
        return true; // root matches every absolute path
    }

    uint32_t plen = (uint32_t)strlen(prefix);
    if (strncmp(path, prefix, plen) != 0)
    {
        return false;
    }

    // the next character must end the component: either end of string or '/'.
    return path[plen] == '\0' || path[plen] == '/';
}

kerr_t vfs_mount(const char *prefix, fs_mount_t *mount)
{
    if (!prefix || !mount || !mount->is_mounted || prefix[0] != '/')
    {
        return KERR_INVAL;
    }

    if (strlen(prefix) >= VFS_PATH_MAX)
    {
        return KERR_INVAL;
    }

    // replacing an existing mount at the same prefix is allowed.
    vfs_mount_entry_t *slot = find_exact(prefix);
    if (!slot)
    {
        for (int i = 0; i < VFS_MOUNT_MAX; i++)
        {
            if (!mount_table[i].mount)
            {
                slot = &mount_table[i];
                break;
            }
        }
    }

    if (!slot)
    {
        return KERR_NOSPC;
    }

    strncpy(slot->prefix, prefix, VFS_PATH_MAX - 1);
    slot->prefix[VFS_PATH_MAX - 1] = '\0';
    slot->mount = mount;
    return KERR_OK;
}

kerr_t vfs_umount(const char *prefix)
{
    if (!prefix)
    {
        return KERR_INVAL;
    }

    vfs_mount_entry_t *slot = find_exact(prefix);
    if (!slot)
    {
        return KERR_NOENT;
    }

    slot->mount = NULL;
    slot->prefix[0] = '\0';
    return KERR_OK;
}

fs_mount_t *vfs_resolve_mount_path(const char *path, const char **rel_out)
{
    if (!path || path[0] != '/')
    {
        return NULL;
    }

    // resolve to the longest-matching mount, tracking its prefix length so we
    // can hand the backing fs a path relative to its own mount point rather
    // than the full absolute path.
    fs_mount_t *best = NULL;
    uint32_t best_len = 0;

    for (int i = 0; i < VFS_MOUNT_MAX; i++)
    {
        if (!mount_table[i].mount || !prefix_covers(mount_table[i].prefix, path))
        {
            continue;
        }

        uint32_t plen = (uint32_t)strlen(mount_table[i].prefix);
        if (best == NULL || plen > best_len)
        {
            best = mount_table[i].mount;
            best_len = plen;
        }
    }

    if (!best)
    {
        return NULL;
    }

    if (rel_out)
    {
        // the root mount "/" owns the whole path: hand it through unchanged so
        // the backing fs still sees a leading '/'. for a sub-mount like "/dev",
        // strip the prefix so "/dev/fb0" becomes "/fb0" and "/dev" becomes "/".
        // the relative path always keeps a leading '/' so backends have a
        // consistent root-relative contract.
        if (best_len <= 1)
        {
            *rel_out = path; // root mount: full path, already leading '/'.
        }
        else
        {
            *rel_out = path + best_len; // points at the '/' after the prefix,
                                        // or at the trailing '\0' for an exact
                                        // mount-point match.
            if ((*rel_out)[0] == '\0')
            {
                *rel_out = "/"; // exact mount point resolves to the fs root.
            }
        }
    }

    return best;
}

kerr_t vfs_mount_root(fs_mount_t *mount)
{
    // the root mount is just the entry at "/".
    return vfs_mount("/", mount);
}

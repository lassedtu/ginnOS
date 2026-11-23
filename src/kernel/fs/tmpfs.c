#include "tmpfs.h"

#include "common/memory.h"
#include "common/string.h"
#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/sync/mutex.h"
#include "kernel/time/clock.h"
#include "kernel/vfs/stat.h"

/**
 * @file tmpfs.c
 * @brief a filesystem that keeps its files in kernel memory.
 *
 * every file and directory is a node on the kernel heap; a file's bytes are
 * one heap buffer that grows as it is written. nothing survives a reboot.
 *
 * an open file keeps its node in fs_data and its offset in fs_pos (for a
 * directory, fs_pos is the index of the next entry). a node removed while
 * it is open stays alive until its last close.
 *
 * a symbolic link is a node whose bytes are its target. there are no hard
 * links: a node is one name.
 *
 * locking: one mutex for the whole filesystem (there is one tmpfs).
 */

// longest name, NUL included
#define TMPFS_NAME_MAX 64

// most bytes all tmpfs files may hold together
#define TMPFS_MAX_BYTES (8u * 1024u * 1024u)

typedef struct tmpfs_node
{
    char name[TMPFS_NAME_MAX];
    uint8_t type;               // FS_TYPE_FILE, FS_TYPE_DIR or FS_TYPE_SYMLINK
    uint32_t inode;
    struct tmpfs_node *parent;  // the root's parent is the root
    struct tmpfs_node *children; // first entry of a directory
    struct tmpfs_node *next;    // next entry in the parent
    uint8_t *data;              // a file's bytes
    uint32_t size;
    uint32_t capacity;
    uint32_t open_count;        // open files that use the node
    bool removed;               // unlinked; freed at the last close
    uint32_t mode;              // permission bits (07777)
    uint32_t uid;
    uint32_t gid;
    uint32_t atime;             // seconds since 1970
    uint32_t mtime;
    uint32_t ctime;
} tmpfs_node_t;

static mutex_t tmpfs_lock = MUTEX_INIT;
static tmpfs_node_t root;
static uint32_t next_inode = 2;
static uint32_t bytes_used;

/**
 * a new node, not yet in a directory.
 */
static tmpfs_node_t *node_new(const char *name, uint8_t type)
{
    tmpfs_node_t *node = kcalloc(1, sizeof(tmpfs_node_t));
    if (node)
    {
        strncpy(node->name, name, TMPFS_NAME_MAX - 1);
        node->type = type;
        node->inode = next_inode++;
        // the creator sets its own mode and owner right after (sys_open)
        node->mode = type == FS_TYPE_DIR ? 0755 : type == FS_TYPE_SYMLINK ? 0777 : 0644;
        node->atime = node->mtime = node->ctime = clock_realtime_seconds();
    }
    return node;
}

static void node_free(tmpfs_node_t *node)
{
    bytes_used -= node->capacity;
    kfree(node->data);
    kfree(node);
}

static tmpfs_node_t *child_named(tmpfs_node_t *dir, const char *name, uint32_t len)
{
    for (tmpfs_node_t *child = dir->children; child; child = child->next)
    {
        if (strncmp(child->name, name, len) == 0 && child->name[len] == '\0')
        {
            return child;
        }
    }
    return NULL;
}

static void attach(tmpfs_node_t *dir, tmpfs_node_t *node)
{
    node->parent = dir;
    node->next = dir->children;
    dir->children = node;
}

static void detach(tmpfs_node_t *node)
{
    tmpfs_node_t **link = &node->parent->children;
    while (*link != node)
    {
        link = &(*link)->next;
    }
    *link = node->next;
    node->next = NULL;
}

/**
 * the node at a mount-relative path ("/", "/a/b"), or NULL.
 */
static tmpfs_node_t *lookup(const char *path)
{
    tmpfs_node_t *node = &root;
    while (*path)
    {
        while (*path == '/')
        {
            path++;
        }
        uint32_t len = 0;
        while (path[len] && path[len] != '/')
        {
            len++;
        }
        if (len == 0)
        {
            break;
        }

        if (len == 1 && path[0] == '.')
        {
            // stays here
        }
        else if (len == 2 && path[0] == '.' && path[1] == '.')
        {
            node = node->parent;
        }
        else
        {
            if (node->type != FS_TYPE_DIR)
            {
                return NULL;
            }
            node = child_named(node, path, len);
            if (!node)
            {
                return NULL;
            }
        }
        path += len;
    }
    return node;
}

/**
 * split a path into its parent directory and last name.
 * @param path the mount-relative path.
 * @param dir_out receives the parent directory.
 * @param name_out receives the last component (NUL-terminated).
 * @return KERR_OK, KERR_NOENT if the parent does not exist, KERR_NOTDIR if
 *         it is a file, KERR_INVAL for a bad name, KERR_RANGE if too long.
 */
static kerr_t split_path(const char *path, tmpfs_node_t **dir_out, char *name_out)
{
    // drop trailing slashes, then find the last one
    uint32_t end = (uint32_t)strlen(path);
    while (end > 1 && path[end - 1] == '/')
    {
        end--;
    }
    uint32_t start = end;
    while (start > 0 && path[start - 1] != '/')
    {
        start--;
    }

    uint32_t len = end - start;
    if (len == 0 || (len == 1 && path[start] == '.') ||
        (len == 2 && path[start] == '.' && path[start + 1] == '.'))
    {
        return KERR_INVAL;
    }
    if (len >= TMPFS_NAME_MAX)
    {
        return KERR_RANGE;
    }
    memcpy(name_out, path + start, len);
    name_out[len] = '\0';

    char parent_path[TMPFS_NAME_MAX * 4];
    if (start >= sizeof(parent_path))
    {
        return KERR_RANGE;
    }
    memcpy(parent_path, path, start);
    parent_path[start] = '\0';

    tmpfs_node_t *dir = lookup(parent_path);
    if (!dir)
    {
        return KERR_NOENT;
    }
    if (dir->type != FS_TYPE_DIR)
    {
        return KERR_NOTDIR;
    }
    *dir_out = dir;
    return KERR_OK;
}

/**
 * add a new node at path.
 */
static kerr_t make_node(const char *path, uint8_t type)
{
    tmpfs_node_t *dir;
    char name[TMPFS_NAME_MAX];
    kerr_t err = split_path(path, &dir, name);
    if (kerr_failed(err))
    {
        return err;
    }
    if (child_named(dir, name, (uint32_t)strlen(name)))
    {
        return KERR_EXIST;
    }

    tmpfs_node_t *node = node_new(name, type);
    if (!node)
    {
        return KERR_NOMEM;
    }
    attach(dir, node);
    return KERR_OK;
}

/**
 * take a node out of its directory; free it now or at its last close.
 */
static void unlink_node(tmpfs_node_t *node)
{
    detach(node);
    if (node->open_count > 0)
    {
        node->removed = true;
    }
    else
    {
        node_free(node);
    }
}

/**
 * make room for size bytes in a file.
 */
static bool reserve(tmpfs_node_t *node, uint32_t size)
{
    if (size <= node->capacity)
    {
        return true;
    }

    uint32_t capacity = node->capacity ? node->capacity : 256;
    while (capacity < size)
    {
        capacity *= 2;
    }
    if (bytes_used - node->capacity + capacity > TMPFS_MAX_BYTES)
    {
        return false;
    }

    uint8_t *data = krealloc(node->data, capacity);
    if (!data)
    {
        return false;
    }
    bytes_used += capacity - node->capacity;
    node->data = data;
    node->capacity = capacity;
    return true;
}

static void fill_stat(const tmpfs_node_t *node, fs_stat_t *stat_out)
{
    memset(stat_out, 0, sizeof(*stat_out));
    stat_out->inode = node->inode;
    stat_out->file_type = node->type;
    uint32_t kind = S_IFREG;
    if (node->type == FS_TYPE_DIR)
    {
        kind = S_IFDIR;
    }
    else if (node->type == FS_TYPE_SYMLINK)
    {
        kind = S_IFLNK;
    }
    stat_out->mode = kind | node->mode;
    stat_out->uid = (uint16_t)node->uid;
    stat_out->gid = (uint16_t)node->gid;
    stat_out->links_count = 1;
    stat_out->size = node->size;
    stat_out->blocks = (node->capacity + 511) / 512;
    stat_out->atime = node->atime;
    stat_out->mtime = node->mtime;
    stat_out->ctime = node->ctime;
}

/*
 * the operations. each takes the filesystem mutex.
 */

static kerr_t tmpfs_open(fs_mount_t *mount, const char *path, fs_file_t *file)
{
    (void)mount;
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = lookup(path);
    if (node)
    {
        node->open_count++;
        file->fs_data = node;
        file->fs_pos = 0;
        file->file_type = node->type;
    }
    mutex_unlock(&tmpfs_lock);
    return node ? KERR_OK : KERR_NOENT;
}

static void tmpfs_close(fs_file_t *file)
{
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = file->fs_data;
    node->open_count--;
    if (node->removed && node->open_count == 0)
    {
        node_free(node);
    }
    file->fs_data = NULL;
    mutex_unlock(&tmpfs_lock);
}

static kerr_t tmpfs_create(fs_mount_t *mount, const char *path)
{
    (void)mount;
    mutex_lock(&tmpfs_lock);
    kerr_t err = make_node(path, FS_TYPE_FILE);
    mutex_unlock(&tmpfs_lock);
    return err;
}

static kerr_t tmpfs_mkdir(fs_mount_t *mount, const char *path)
{
    (void)mount;
    mutex_lock(&tmpfs_lock);
    kerr_t err = make_node(path, FS_TYPE_DIR);
    mutex_unlock(&tmpfs_lock);
    return err;
}

static kerr_t tmpfs_remove(fs_mount_t *mount, const char *path)
{
    (void)mount;
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = lookup(path);
    kerr_t err = KERR_OK;
    if (!node)
    {
        err = KERR_NOENT;
    }
    else if (node->type == FS_TYPE_DIR)
    {
        err = KERR_ISDIR;
    }
    else
    {
        unlink_node(node);
    }
    mutex_unlock(&tmpfs_lock);
    return err;
}

static kerr_t tmpfs_rmdir(fs_mount_t *mount, const char *path)
{
    (void)mount;
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = lookup(path);
    kerr_t err = KERR_OK;
    if (!node)
    {
        err = KERR_NOENT;
    }
    else if (node->type != FS_TYPE_DIR)
    {
        err = KERR_NOTDIR;
    }
    else if (node == &root)
    {
        err = KERR_BUSY;
    }
    else if (node->children)
    {
        err = KERR_NOTEMPTY;
    }
    else
    {
        unlink_node(node);
    }
    mutex_unlock(&tmpfs_lock);
    return err;
}

/**
 * true if node is dir or inside it.
 */
static bool is_within(const tmpfs_node_t *node, const tmpfs_node_t *dir)
{
    for (;;)
    {
        if (node == dir)
        {
            return true;
        }
        if (node == &root)
        {
            return false;
        }
        node = node->parent;
    }
}

static kerr_t rename_locked(const char *old_path, const char *new_path)
{
    tmpfs_node_t *node = lookup(old_path);
    if (!node)
    {
        return KERR_NOENT;
    }
    if (node == &root)
    {
        return KERR_BUSY;
    }

    tmpfs_node_t *dir;
    char name[TMPFS_NAME_MAX];
    kerr_t err = split_path(new_path, &dir, name);
    if (kerr_failed(err))
    {
        return err;
    }
    if (node->type == FS_TYPE_DIR && is_within(dir, node))
    {
        return KERR_INVAL; // a directory cannot move into itself
    }

    // an existing target is replaced, if it is the same kind (and empty)
    tmpfs_node_t *target = child_named(dir, name, (uint32_t)strlen(name));
    if (target == node)
    {
        return KERR_OK;
    }
    if (target)
    {
        if (target->type != node->type)
        {
            return target->type == FS_TYPE_DIR ? KERR_ISDIR : KERR_NOTDIR;
        }
        if (target->children)
        {
            return KERR_NOTEMPTY;
        }
        unlink_node(target);
    }

    detach(node);
    strncpy(node->name, name, TMPFS_NAME_MAX - 1);
    node->name[TMPFS_NAME_MAX - 1] = '\0';
    attach(dir, node);
    return KERR_OK;
}

static kerr_t tmpfs_rename(fs_mount_t *mount, const char *old_path, const char *new_path)
{
    (void)mount;
    mutex_lock(&tmpfs_lock);
    kerr_t err = rename_locked(old_path, new_path);
    mutex_unlock(&tmpfs_lock);
    return err;
}

static kerr_t tmpfs_stat(fs_mount_t *mount, const char *path, fs_stat_t *stat_out)
{
    (void)mount;
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = lookup(path);
    if (node)
    {
        fill_stat(node, stat_out);
    }
    mutex_unlock(&tmpfs_lock);
    return node ? KERR_OK : KERR_NOENT;
}

static kerr_t tmpfs_fstat(fs_file_t *file, fs_stat_t *stat_out)
{
    mutex_lock(&tmpfs_lock);
    fill_stat(file->fs_data, stat_out);
    mutex_unlock(&tmpfs_lock);
    return KERR_OK;
}

static uint32_t tmpfs_read(fs_file_t *file, uint32_t count, void *data_out)
{
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = file->fs_data;
    uint32_t n = 0;
    if (node->type == FS_TYPE_FILE && file->fs_pos < node->size)
    {
        n = node->size - file->fs_pos;
        if (n > count)
        {
            n = count;
        }
        memcpy(data_out, node->data + file->fs_pos, n);
        file->fs_pos += n;
    }
    mutex_unlock(&tmpfs_lock);
    return n;
}

static uint32_t tmpfs_write(fs_file_t *file, uint32_t count, const void *data_in)
{
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = file->fs_data;
    uint32_t n = 0;
    uint32_t end = file->fs_pos + count;
    if (node->type == FS_TYPE_FILE && end >= file->fs_pos && reserve(node, end))
    {
        // a write past the end leaves a hole of zeros
        if (file->fs_pos > node->size)
        {
            memset(node->data + node->size, 0, file->fs_pos - node->size);
        }
        memcpy(node->data + file->fs_pos, data_in, count);
        file->fs_pos = end;
        if (end > node->size)
        {
            node->size = end;
        }
        node->mtime = node->ctime = clock_realtime_seconds();
        n = count;
    }
    mutex_unlock(&tmpfs_lock);
    return n;
}

static kerr_t tmpfs_truncate(fs_file_t *file)
{
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = file->fs_data;
    node->size = 0;
    file->fs_pos = 0;
    node->mtime = node->ctime = clock_realtime_seconds();
    mutex_unlock(&tmpfs_lock);
    return KERR_OK;
}

static kerr_t tmpfs_read_entry(fs_file_t *file, fs_dirent_t *entry_out)
{
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *dir = file->fs_data;
    kerr_t err = KERR_OK;

    if (dir->type != FS_TYPE_DIR)
    {
        err = KERR_NOTDIR;
    }
    else if (file->fs_pos < 2)
    {
        // "." and ".." come first, as on ext2
        tmpfs_node_t *self = file->fs_pos == 0 ? dir : dir->parent;
        entry_out->inode = self->inode;
        entry_out->file_type = FS_TYPE_DIR;
        entry_out->size = 0;
        strcpy(entry_out->name, file->fs_pos == 0 ? "." : "..");
        file->fs_pos++;
    }
    else
    {
        tmpfs_node_t *child = dir->children;
        for (uint32_t i = 2; child && i < file->fs_pos; i++)
        {
            child = child->next;
        }
        if (!child)
        {
            err = KERR_NOENT;
        }
        else
        {
            entry_out->inode = child->inode;
            entry_out->file_type = child->type;
            entry_out->size = child->size;
            strncpy(entry_out->name, child->name, sizeof(entry_out->name) - 1);
            entry_out->name[sizeof(entry_out->name) - 1] = '\0';
            file->fs_pos++;
        }
    }

    mutex_unlock(&tmpfs_lock);
    return err;
}

static int32_t tmpfs_seek(fs_file_t *file, int32_t offset, int whence)
{
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = file->fs_data;
    int32_t base = 0;
    if (whence == 1)
    {
        base = (int32_t)file->fs_pos;
    }
    else if (whence == 2)
    {
        base = (int32_t)node->size;
    }

    int32_t position = base + offset;
    if (whence < 0 || whence > 2 || position < 0)
    {
        position = -EINVAL;
    }
    else
    {
        file->fs_pos = (uint32_t)position;
    }
    mutex_unlock(&tmpfs_lock);
    return position;
}

static kerr_t tmpfs_setattr(fs_file_t *file, const fs_attr_t *attr)
{
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = file->fs_data;
    if (attr->what & FS_ATTR_MODE)
    {
        node->mode = attr->mode & 07777u;
    }
    if (attr->what & FS_ATTR_OWNER)
    {
        node->uid = attr->uid;
        node->gid = attr->gid;
    }
    if (attr->what & FS_ATTR_ATIME)
    {
        node->atime = attr->atime;
    }
    if (attr->what & FS_ATTR_MTIME)
    {
        node->mtime = attr->mtime;
    }
    node->ctime = clock_realtime_seconds();
    mutex_unlock(&tmpfs_lock);
    return KERR_OK;
}

/**
 * copy a link node's target, with a NUL.
 */
static kerr_t link_target(const tmpfs_node_t *node, char *target, uint32_t size)
{
    if (node->size >= size)
    {
        return KERR_RANGE;
    }
    memcpy(target, node->data, node->size);
    target[node->size] = '\0';
    return KERR_OK;
}

static kerr_t tmpfs_find_link(fs_mount_t *mount, const char *path, bool follow_last,
                              char *target, uint32_t size, uint32_t *end_out)
{
    (void)mount;
    *end_out = 0;
    mutex_lock(&tmpfs_lock);
    kerr_t err = KERR_OK;
    tmpfs_node_t *node = &root;
    uint32_t i = 0;
    while (path[i] != '\0' && node->type == FS_TYPE_DIR)
    {
        while (path[i] == '/')
        {
            i++;
        }
        uint32_t start = i;
        while (path[i] != '\0' && path[i] != '/')
        {
            i++;
        }
        if (i == start)
        {
            break;
        }
        // the path is canonical: no "." or ".." names
        node = child_named(node, path + start, i - start);
        if (!node)
        {
            break;
        }
        uint32_t after = i;
        while (path[after] == '/')
        {
            after++;
        }
        if (node->type == FS_TYPE_SYMLINK && (follow_last || path[after] != '\0'))
        {
            err = link_target(node, target, size);
            if (kerr_ok(err))
            {
                *end_out = i;
            }
            break;
        }
    }
    mutex_unlock(&tmpfs_lock);
    return err;
}

static kerr_t tmpfs_readlink(fs_mount_t *mount, const char *path, char *target, uint32_t size)
{
    (void)mount;
    mutex_lock(&tmpfs_lock);
    tmpfs_node_t *node = lookup(path);
    kerr_t err = KERR_NOENT;
    if (node)
    {
        err = node->type == FS_TYPE_SYMLINK ? link_target(node, target, size) : KERR_INVAL;
    }
    mutex_unlock(&tmpfs_lock);
    return err;
}

static kerr_t tmpfs_symlink(fs_mount_t *mount, const char *path, const char *target)
{
    (void)mount;
    mutex_lock(&tmpfs_lock);
    kerr_t err = make_node(path, FS_TYPE_SYMLINK);
    if (kerr_ok(err))
    {
        tmpfs_node_t *node = lookup(path);
        uint32_t length = (uint32_t)strlen(target);
        if (reserve(node, length))
        {
            memcpy(node->data, target, length);
            node->size = length;
        }
        else
        {
            unlink_node(node);
            err = KERR_NOSPC;
        }
    }
    mutex_unlock(&tmpfs_lock);
    return err;
}

static const fs_ops_t tmpfs_ops = {
    .open = tmpfs_open,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir,
    .remove = tmpfs_remove,
    .rmdir = tmpfs_rmdir,
    .rename = tmpfs_rename,
    .stat = tmpfs_stat,
    .fstat = tmpfs_fstat,
    .setattr = tmpfs_setattr,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .truncate = tmpfs_truncate,
    .read_entry = tmpfs_read_entry,
    .close = tmpfs_close,
    .seek = tmpfs_seek,
    .find_link = tmpfs_find_link,
    .readlink = tmpfs_readlink,
    .symlink = tmpfs_symlink,
};

kerr_t tmpfs_mount(fs_mount_t *mount)
{
    if (!mount)
    {
        return KERR_INVAL;
    }

    memset(&root, 0, sizeof(root));
    root.type = FS_TYPE_DIR;
    root.inode = 1;
    root.parent = &root;
    // like a usual /tmp: anyone may make files, only owners remove them
    root.mode = 01777;

    mount->ops = &tmpfs_ops;
    mount->fs_data = &root;
    mount->is_mounted = 1;
    return KERR_OK;
}

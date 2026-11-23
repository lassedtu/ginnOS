#include "file.h"

#include "kernel/errno.h"
#include "kernel/vfs/poll.h"
#include "kernel/device/device.h"
#include "kernel/fs/devfs.h"
#include "kernel/vfs/flock.h"
#include "kernel/memory/heap.h"
#include "kernel/sync/spinlock.h"
#include "kernel/assert.h"
#include "common/memory.h"
#include "common/string.h"

// guards file_t::refcount of every description
static spinlock_t file_lock;

/**
 * a new description with one reference, or NULL if the heap is full.
 */
static file_t *file_alloc(const file_ops_t *ops, uint32_t flags)
{
    file_t *file = kmalloc(sizeof(file_t));
    if (file)
    {
        file->ops = ops;
        file->refcount = 1;
        file->flags = flags;
        file->lookahead = NULL;
        file->path = NULL; // only a filesystem file has one (file_open)
    }
    return file;
}

file_t *file_new(const file_ops_t *ops, uint32_t flags)
{
    return file_alloc(ops, flags);
}

void file_ref(file_t *file)
{
    uint32_t flags = spin_lock_irqsave(&file_lock);
    KASSERT(file->refcount > 0, "file_ref: description already released");
    file->refcount++;
    spin_unlock_irqrestore(&file_lock, flags);
}

void file_unref(file_t *file)
{
    if (!file)
    {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&file_lock);
    KASSERT(file->refcount > 0, "file_unref: description already released");
    bool last = --file->refcount == 0;
    spin_unlock_irqrestore(&file_lock, flags);

    // the release can sleep (an ext2 close takes the fs mutex), so it runs
    // after the spinlock is dropped. nothing else refers to the file now.
    if (last && file->ops->release)
    {
        file->ops->release(file);
    }
}

bool file_readable(const file_t *file)
{
    return (file->flags & O_ACCMODE) != O_WRONLY;
}

bool file_writable(const file_t *file)
{
    return (file->flags & O_ACCMODE) != O_RDONLY;
}

int32_t file_read(file_t *file, void *buf, uint32_t count)
{
    if (!file_readable(file) || !file->ops->read)
    {
        return -EBADF;
    }
    return file->ops->read(file, buf, count);
}

int32_t file_write(file_t *file, const void *buf, uint32_t count)
{
    if (!file_writable(file) || !file->ops->write)
    {
        return -EBADF;
    }
    return file->ops->write(file, buf, count);
}

int32_t file_seek(file_t *file, int32_t offset, int whence)
{
    return file->ops->seek ? file->ops->seek(file, offset, whence) : -ESPIPE;
}

int32_t file_ioctl(file_t *file, uint32_t request, void *arg)
{
    return file->ops->ioctl ? file->ops->ioctl(file, request, arg) : -ENOTTY;
}

int32_t file_mmap(file_t *file, addr_space_t as, uintptr_t virt, size_t length, int prot,
                  uint32_t offset)
{
    return file->ops->mmap ? file->ops->mmap(file, as, virt, length, prot, offset) : -ENODEV;
}

int32_t file_read_entry(file_t *file, fs_dirent_t *entry)
{
    if (file->lookahead)
    {
        *entry = *file->lookahead;
        kfree(file->lookahead);
        file->lookahead = NULL;
        return 0;
    }
    return file->ops->read_entry ? file->ops->read_entry(file, entry) : -ENOTDIR;
}

int32_t file_unread_entry(file_t *file, const fs_dirent_t *entry)
{
    KASSERT(!file->lookahead, "file_unread_entry: an entry is already given back");
    file->lookahead = kmalloc(sizeof(fs_dirent_t));
    if (!file->lookahead)
    {
        return -ENOMEM;
    }
    *file->lookahead = *entry;
    return 0;
}

int32_t file_truncate(file_t *file, uint32_t length)
{
    if (!file_writable(file) || !file->ops->truncate)
    {
        return -EINVAL;
    }
    return file->ops->truncate(file, length);
}

uint32_t file_poll(file_t *file)
{
    if (file->ops->poll)
    {
        return file->ops->poll(file);
    }
    uint32_t events = 0;
    if (file_readable(file))
    {
        events |= POLLIN;
    }
    if (file_writable(file))
    {
        events |= POLLOUT;
    }
    return events;
}

int32_t file_stat(file_t *file, struct kstat *st)
{
    return file->ops->stat ? file->ops->stat(file, st) : -EINVAL;
}

/**
 * move to offset, run a read or write, and move back.
 */
static int32_t at_offset(file_t *file, void *buf, uint32_t count, int32_t offset, bool write)
{
    if (!file->ops->seek)
    {
        return -ESPIPE;
    }
    if (offset < 0)
    {
        return -EINVAL;
    }

    int32_t saved = file->ops->seek(file, 0, VFS_SEEK_CUR);
    if (saved < 0)
    {
        return saved;
    }
    int32_t moved = file->ops->seek(file, offset, VFS_SEEK_SET);
    if (moved < 0)
    {
        return moved;
    }

    int32_t result = write ? file_write(file, buf, count) : file_read(file, buf, count);
    file->ops->seek(file, saved, VFS_SEEK_SET);
    return result;
}

int32_t file_pread(file_t *file, void *buf, uint32_t count, int32_t offset)
{
    return at_offset(file, buf, count, offset, false);
}

int32_t file_pwrite(file_t *file, const void *buf, uint32_t count, int32_t offset)
{
    // O_APPEND would move the write to the end
    if (file->flags & O_APPEND)
    {
        return -EINVAL;
    }
    return at_offset(file, (void *)buf, count, offset, true);
}

/*
 * filesystem files: a vfs_file_t, which holds the offset (the ext2 cursor
 * or the generic fs_pos), so sharing the description shares the offset.
 */

static int32_t vfs_kind_read(file_t *file, void *buf, uint32_t count)
{
    return (int32_t)vfs_read(&file->vfs, count, buf);
}

static int32_t vfs_kind_write(file_t *file, const void *buf, uint32_t count)
{
    if (file->flags & O_APPEND)
    {
        // every write goes to the end, also after another writer grew it
        vfs_seek_end(&file->vfs);
    }
    return (int32_t)vfs_write(&file->vfs, count, buf);
}

static int32_t vfs_kind_seek(file_t *file, int32_t offset, int whence)
{
    return vfs_lseek(&file->vfs, offset, whence);
}

static int32_t vfs_kind_ioctl(file_t *file, uint32_t request, void *arg)
{
    return vfs_ioctl(&file->vfs, request, arg);
}

static int32_t vfs_kind_mmap(file_t *file, addr_space_t as, uintptr_t virt, size_t length,
                             int prot, uint32_t offset)
{
    return vfs_mmap(&file->vfs, as, virt, length, prot, offset);
}

static int32_t vfs_kind_read_entry(file_t *file, fs_dirent_t *entry)
{
    return kerr_to_errno(vfs_read_entry(&file->vfs, entry));
}

static int32_t vfs_kind_truncate(file_t *file, uint32_t length)
{
    return kerr_to_errno(vfs_truncate(&file->vfs, length));
}

static int32_t vfs_kind_stat(file_t *file, struct kstat *st)
{
    fs_stat_t fs_stat;
    kerr_t err = vfs_fstat(&file->vfs, &fs_stat);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }
    stat_from_fs(&fs_stat, st);
    return 0;
}

static void vfs_kind_release(file_t *file)
{
    flock_release(file);
    kfree(file->lookahead);
    kfree(file->path);
    vfs_close(&file->vfs);
    kfree(file);
}

static const file_ops_t vfs_file_ops = {
    .read = vfs_kind_read,
    .write = vfs_kind_write,
    .seek = vfs_kind_seek,
    .ioctl = vfs_kind_ioctl,
    .mmap = vfs_kind_mmap,
    .read_entry = vfs_kind_read_entry,
    .truncate = vfs_kind_truncate,
    .stat = vfs_kind_stat,
    .release = vfs_kind_release,
};

bool file_is_vfs(const file_t *file)
{
    return file->ops == &vfs_file_ops;
}

kerr_t file_open(const char *path, uint32_t flags, file_t **out)
{
    file_t *file = file_alloc(&vfs_file_ops, flags);
    if (!file)
    {
        return KERR_NOMEM;
    }

    kerr_t err = vfs_open(path, &file->vfs);
    if (kerr_failed(err))
    {
        kfree(file);
        return err;
    }

    // a device with its own stream operations makes its own description
    device_t *dev = devfs_device(&file->vfs.file);
    if (dev && dev->ops && dev->ops->open)
    {
        vfs_close(&file->vfs);
        kfree(file);
        return dev->ops->open(dev, flags, out);
    }

    // the path, for the *at calls that name a file in this directory
    size_t length = strlen(path) + 1;
    file->path = kmalloc(length);
    if (file->path)
    {
        memcpy(file->path, path, length);
    }

    *out = file;
    return KERR_OK;
}

/*
 * pipe ends: the description holds one reference to its end of the pipe,
 * dropped when the description is released.
 */

static int32_t pipe_kind_read(file_t *file, void *buf, uint32_t count)
{
    return pipe_read(file->pipe.buf, buf, count, (file->flags & O_NONBLOCK) != 0);
}

static int32_t pipe_kind_write(file_t *file, const void *buf, uint32_t count)
{
    return pipe_write(file->pipe.buf, buf, count, (file->flags & O_NONBLOCK) != 0);
}

static int32_t pipe_kind_stat(file_t *file, struct kstat *st)
{
    (void)file;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFIFO | 0600;
    st->st_nlink = 1;
    st->st_blksize = PIPE_BUF;
    return 0;
}

static uint32_t pipe_kind_poll(file_t *file)
{
    return pipe_poll(file->pipe.buf, file->pipe.dir);
}

static void pipe_kind_release(file_t *file)
{
    pipe_unref(file->pipe.buf, file->pipe.dir);
    kfree(file);
}

static const file_ops_t pipe_file_ops = {
    .read = pipe_kind_read,
    .write = pipe_kind_write,
    .stat = pipe_kind_stat,
    .poll = pipe_kind_poll,
    .release = pipe_kind_release,
};

kerr_t file_open_pipe(file_t **read_end, file_t **write_end)
{
    file_t *reader = file_alloc(&pipe_file_ops, O_RDONLY);
    file_t *writer = file_alloc(&pipe_file_ops, O_WRONLY);
    pipe_buf_t *buf = pipe_alloc();
    if (!reader || !writer || !buf)
    {
        kfree(reader);
        kfree(writer);
        if (buf)
        {
            pipe_unref(buf, PIPE_READ);
            pipe_unref(buf, PIPE_WRITE);
        }
        return KERR_NOMEM;
    }

    reader->pipe.buf = buf;
    reader->pipe.dir = PIPE_READ;
    writer->pipe.buf = buf;
    writer->pipe.dir = PIPE_WRITE;
    *read_end = reader;
    *write_end = writer;
    return KERR_OK;
}

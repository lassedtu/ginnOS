#include "fd_table.h"

#include "kernel/errno.h"
#include "kernel/process/process.h"

int fd_install(process_t *proc, file_t *file, uint32_t fd_flags, int min_fd)
{
    for (int fd = min_fd < 0 ? 0 : min_fd; fd < FD_MAX; fd++)
    {
        if (!proc->fds[fd].file)
        {
            proc->fds[fd].file = file;
            proc->fds[fd].flags = fd_flags;
            return fd;
        }
    }
    return -EMFILE;
}

fd_entry_t *fd_get(int fd)
{
    process_t *proc = process_current();
    if (!proc || fd < 0 || fd >= FD_MAX || !proc->fds[fd].file)
    {
        return NULL;
    }
    return &proc->fds[fd];
}

int fd_close(int fd)
{
    fd_entry_t *entry = fd_get(fd);
    if (!entry)
    {
        return -EBADF;
    }
    fd_close_entry(entry);
    return 0;
}

void fd_close_entry(fd_entry_t *entry)
{
    // free the slot first: the release may sleep, and the slot must not
    // look open meanwhile
    file_t *file = entry->file;
    entry->file = NULL;
    entry->flags = 0;
    file_unref(file);
}

void fd_copy_entry(fd_entry_t *dst, const fd_entry_t *src)
{
    dst->file = src->file;
    dst->flags = src->flags;
    if (dst->file)
    {
        file_ref(dst->file);
    }
}

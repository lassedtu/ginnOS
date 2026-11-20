#include "fd_table.h"
#include "kernel/process/process.h"
#include "common/memory.h"

void fd_table_init(void)
{
    pipe_init();
}

int fd_alloc(vfs_file_t *file)
{
    process_t *proc = process_current();
    if (!proc)
    {
        return -1;
    }

    /* start searching at fd 3 (0/1/2 are reserved) */
    for (int i = 3; i < FD_MAX; i++)
    {
        if (proc->fds[i].type == FD_TYPE_NONE)
        {
            proc->fds[i].type = FD_TYPE_FILE;
            proc->fds[i].file = *file;
            return i;
        }
    }

    return -1;
}

fd_entry_t *fd_get(int fd)
{
    process_t *proc = process_current();
    if (!proc)
    {
        return NULL;
    }

    if (fd < 0 || fd >= FD_MAX)
    {
        return NULL;
    }

    if (proc->fds[fd].type == FD_TYPE_NONE)
    {
        return NULL;
    }

    return &proc->fds[fd];
}

int fd_free(int fd)
{
    process_t *proc = process_current();
    if (!proc)
    {
        return -1;
    }

    if (fd < 0 || fd >= FD_MAX)
    {
        return -1;
    }

    if (proc->fds[fd].type == FD_TYPE_NONE)
    {
        return -1;
    }

    fd_close_entry(&proc->fds[fd]);
    return 0;
}

void fd_close_entry(fd_entry_t *entry)
{
    if (entry->type == FD_TYPE_FILE)
    {
        vfs_close(&entry->file);
    }
    else if (entry->type == FD_TYPE_PIPE)
    {
        pipe_unref(entry->pipe.buf, entry->pipe.dir);
    }

    entry->type = FD_TYPE_NONE;
}

void fd_copy_entry(fd_entry_t *dst, const fd_entry_t *src)
{
    *dst = *src;
    if (dst->type == FD_TYPE_PIPE)
    {
        pipe_ref(dst->pipe.buf, dst->pipe.dir);
    }
}

#include "fd_table.h"
#include "../process/process.h"
#include "../../common/memory.h"

void fd_table_init(void)
{
    /* no-op: per-process fd tables are initialized in process_create(). */
}

int fd_alloc(VFS_FILE *file)
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
        return (void *)0;
    }

    if (fd < 0 || fd >= FD_MAX)
    {
        return (void *)0;
    }

    if (proc->fds[fd].type == FD_TYPE_NONE)
    {
        return (void *)0;
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

    if (proc->fds[fd].type == FD_TYPE_FILE)
    {
        vfs_close(&proc->fds[fd].file);
    }

    proc->fds[fd].type = FD_TYPE_NONE;
    return 0;
}

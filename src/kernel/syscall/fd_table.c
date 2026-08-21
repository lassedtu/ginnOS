#include "fd_table.h"
#include "kernel/process/process.h"
#include "common/memory.h"

// system-wide pipe buffer pool
static pipe_buf_t pipe_pool[PIPE_MAX];

void fd_table_init(void)
{
    // initialize pipe pool
    for (int i = 0; i < PIPE_MAX; i++)
    {
        pipe_pool[i].ref_count = 0;
        pipe_pool[i].read_refs = 0;
        pipe_pool[i].write_refs = 0;
    }
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

    if (proc->fds[fd].type == FD_TYPE_FILE)
    {
        vfs_close(&proc->fds[fd].file);
    }
    else if (proc->fds[fd].type == FD_TYPE_PIPE)
    {
        pipe_buf_t *buf = proc->fds[fd].pipe.buf;
        pipe_dir_t dir = proc->fds[fd].pipe.dir;

        if (dir == PIPE_READ)
            buf->read_refs--;
        else
            buf->write_refs--;

        buf->ref_count--;
        if (buf->ref_count <= 0)
            pipe_release(buf);
    }

    proc->fds[fd].type = FD_TYPE_NONE;
    return 0;
}

pipe_buf_t *pipe_alloc(void)
{
    for (int i = 0; i < PIPE_MAX; i++)
    {
        if (pipe_pool[i].ref_count == 0)
        {
            pipe_pool[i].read_pos = 0;
            pipe_pool[i].write_pos = 0;
            pipe_pool[i].count = 0;
            pipe_pool[i].read_refs = 1;
            pipe_pool[i].write_refs = 1;
            pipe_pool[i].ref_count = 2; // read end + write end
            return &pipe_pool[i];
        }
    }
    return NULL;
}

void pipe_release(pipe_buf_t *buf)
{
    buf->ref_count = 0;
    buf->read_refs = 0;
    buf->write_refs = 0;
    buf->count = 0;
    buf->read_pos = 0;
    buf->write_pos = 0;
}

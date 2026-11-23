#include "fifo.h"

#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"
#include "kernel/syscall/pipe.h"

// most named pipes open at once
#define FIFO_MAX 32

typedef struct
{
    fs_mount_t *mount; // the named pipe: its filesystem and inode (NULL: free)
    uint32_t inode;
    pipe_buf_t *pipe;
    uint32_t readers;      // read ends open now
    uint32_t writers;      // write ends open now
    uint32_t reader_opens; // read ends ever opened (a wait ends when it moves)
    uint32_t writer_opens;
} fifo_t;

// a zeroed spinlock and wait queue are ready to use
static fifo_t fifos[FIFO_MAX];
static spinlock_t fifo_lock;
static wait_queue_t fifo_waiters; // all who wait for the other end

static fifo_t *fifo_find(fs_mount_t *mount, uint32_t inode)
{
    for (int i = 0; i < FIFO_MAX; i++)
    {
        if (fifos[i].mount == mount && fifos[i].inode == inode)
        {
            return &fifos[i];
        }
    }
    return NULL;
}

static fifo_t *fifo_of_pipe(const pipe_buf_t *pipe)
{
    for (int i = 0; i < FIFO_MAX; i++)
    {
        if (fifos[i].mount && fifos[i].pipe == pipe)
        {
            return &fifos[i];
        }
    }
    return NULL;
}

/**
 * close ends of a named pipe. the pipe frees itself with its last end;
 * then the slot is free. call with fifo_lock held.
 */
static void drop_ends(fifo_t *f, bool reads, bool writes)
{
    if (reads)
    {
        f->readers--;
        pipe_unref(f->pipe, PIPE_READ);
    }
    if (writes)
    {
        f->writers--;
        pipe_unref(f->pipe, PIPE_WRITE);
    }
    if (f->readers == 0 && f->writers == 0)
    {
        f->mount = NULL;
        f->pipe = NULL;
    }
}

int32_t fifo_open(fs_mount_t *mount, uint32_t inode, uint32_t flags, file_t **out)
{
    uint32_t access = flags & O_ACCMODE;
    bool reads = access != O_WRONLY;
    bool writes = access != O_RDONLY;
    bool nonblock = (flags & O_NONBLOCK) != 0;

    // made before the lock is taken; the pipe goes back if one is open
    file_t *file = file_new_fifo_end(flags);
    pipe_buf_t *fresh = pipe_alloc();
    if (!file || !fresh)
    {
        kfree(file);
        if (fresh)
        {
            pipe_unref(fresh, PIPE_READ);
            pipe_unref(fresh, PIPE_WRITE);
        }
        return -ENOMEM;
    }

    uint32_t irq = spin_lock_irqsave(&fifo_lock);
    int32_t result = 0;
    fifo_t *f = fifo_find(mount, inode);
    if (!f)
    {
        f = fifo_find(NULL, 0);
        if (f)
        {
            *f = (fifo_t){.mount = mount, .inode = inode, .pipe = fresh};
            fresh = NULL;
            // the ends are counted as they open (see below)
            f->pipe->read_refs = 0;
            f->pipe->write_refs = 0;
        }
    }
    if (!f)
    {
        result = -ENFILE;
    }
    else if (access == O_WRONLY && nonblock && f->readers == 0)
    {
        // ENXIO, and a slot just taken for this open is free again
        result = -ENXIO;
        if (f->readers == 0 && f->writers == 0)
        {
            f->writers++;
            pipe_ref(f->pipe, PIPE_WRITE);
            drop_ends(f, false, true);
        }
    }
    else
    {
        uint32_t reader_opens = f->reader_opens;
        uint32_t writer_opens = f->writer_opens;
        if (reads)
        {
            f->readers++;
            f->reader_opens++;
            pipe_ref(f->pipe, PIPE_READ);
        }
        if (writes)
        {
            f->writers++;
            f->writer_opens++;
            pipe_ref(f->pipe, PIPE_WRITE);
        }
        wait_queue_wake_all(&fifo_waiters);

        // one end waits for the other: until one is open, or opened since
        while (!nonblock && access != O_RDWR)
        {
            bool other = access == O_RDONLY
                             ? f->writers > 0 || f->writer_opens != writer_opens
                             : f->readers > 0 || f->reader_opens != reader_opens;
            if (other)
            {
                break;
            }
            if (signal_pending(process_current()))
            {
                drop_ends(f, reads, writes);
                result = -ERESTARTSYS;
                break;
            }
            wait_queue_block_locked_interruptible(&fifo_waiters, &fifo_lock);
        }
        if (result == 0)
        {
            file->pipe.buf = f->pipe;
            file->pipe.dir = reads ? PIPE_READ : PIPE_WRITE;
        }
    }
    spin_unlock_irqrestore(&fifo_lock, irq);

    if (fresh)
    {
        pipe_unref(fresh, PIPE_READ);
        pipe_unref(fresh, PIPE_WRITE);
    }
    if (result < 0)
    {
        kfree(file);
        return result;
    }
    *out = file;
    return 0;
}

void fifo_close(file_t *file)
{
    uint32_t access = file->flags & O_ACCMODE;
    uint32_t irq = spin_lock_irqsave(&fifo_lock);
    fifo_t *f = fifo_of_pipe(file->pipe.buf);
    if (f)
    {
        drop_ends(f, access != O_WRONLY, access != O_RDONLY);
    }
    spin_unlock_irqrestore(&fifo_lock, irq);
}

#include "pipe.h"

#include "common/memory.h"
#include "kernel/assert.h"

// system-wide pipe pool
static pipe_buf_t pipe_pool[PIPE_MAX];

// guards pipe_buf_t::in_use for every pipe in the pool
static spinlock_t pool_lock;

/**
 * return a fully closed pipe to the pool. nothing references it any more,
 * so only the in_use flag needs the pool lock.
 */
static void pipe_free(pipe_buf_t *pipe)
{
    uint32_t flags = spin_lock_irqsave(&pool_lock);
    pipe->in_use = false;
    spin_unlock_irqrestore(&pool_lock, flags);
}

/**
 * copy bytes into the ring at write_pos. caller holds the pipe lock and has
 * checked that count bytes of space are free.
 */
static void ring_put(pipe_buf_t *pipe, const char *src, uint32_t count)
{
    uint32_t first = PIPE_BUF_SIZE - pipe->write_pos;
    if (first > count)
    {
        first = count;
    }

    memcpy(pipe->data + pipe->write_pos, src, first);
    memcpy(pipe->data, src + first, count - first);

    pipe->write_pos = (pipe->write_pos + count) % PIPE_BUF_SIZE;
    pipe->count += count;
}

/**
 * copy bytes out of the ring at read_pos. caller holds the pipe lock and has
 * checked that count bytes are buffered.
 */
static void ring_get(pipe_buf_t *pipe, char *dst, uint32_t count)
{
    uint32_t first = PIPE_BUF_SIZE - pipe->read_pos;
    if (first > count)
    {
        first = count;
    }

    memcpy(dst, pipe->data + pipe->read_pos, first);
    memcpy(dst + first, pipe->data, count - first);

    pipe->read_pos = (pipe->read_pos + count) % PIPE_BUF_SIZE;
    pipe->count -= count;
}

void pipe_init(void)
{
    spinlock_init(&pool_lock);
    for (uint32_t i = 0; i < PIPE_MAX; i++)
    {
        pipe_pool[i].in_use = false;
    }
}

pipe_buf_t *pipe_alloc(void)
{
    pipe_buf_t *pipe = NULL;

    uint32_t flags = spin_lock_irqsave(&pool_lock);
    for (uint32_t i = 0; i < PIPE_MAX; i++)
    {
        if (!pipe_pool[i].in_use)
        {
            pipe = &pipe_pool[i];
            pipe->in_use = true;
            break;
        }
    }
    spin_unlock_irqrestore(&pool_lock, flags);

    if (!pipe)
    {
        return NULL;
    }

    // claimed and not yet handed out, so nothing else can see it
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->count = 0;
    pipe->read_refs = 1;
    pipe->write_refs = 1;
    spinlock_init(&pipe->lock);
    wait_queue_init(&pipe->readers);
    wait_queue_init(&pipe->writers);
    return pipe;
}

void pipe_ref(pipe_buf_t *pipe, pipe_dir_t dir)
{
    uint32_t flags = spin_lock_irqsave(&pipe->lock);

    if (dir == PIPE_READ)
    {
        pipe->read_refs++;
    }
    else
    {
        pipe->write_refs++;
    }

    spin_unlock_irqrestore(&pipe->lock, flags);
}

void pipe_unref(pipe_buf_t *pipe, pipe_dir_t dir)
{
    uint32_t flags = spin_lock_irqsave(&pipe->lock);

    if (dir == PIPE_READ)
    {
        KASSERT(pipe->read_refs > 0, "pipe_unref: no read end open");
        pipe->read_refs--;
        // last reader gone: wake blocked writers so they see the broken
        // pipe instead of waiting forever for space that will never free
        if (pipe->read_refs == 0)
        {
            wait_queue_wake_all(&pipe->writers);
        }
    }
    else
    {
        KASSERT(pipe->write_refs > 0, "pipe_unref: no write end open");
        pipe->write_refs--;
        // last writer gone: wake blocked readers so they observe EOF
        if (pipe->write_refs == 0)
        {
            wait_queue_wake_all(&pipe->readers);
        }
    }

    bool closed = pipe->read_refs == 0 && pipe->write_refs == 0;

    spin_unlock_irqrestore(&pipe->lock, flags);

    if (closed)
    {
        pipe_free(pipe);
    }
}

uint32_t pipe_read(pipe_buf_t *pipe, char *buf, uint32_t count)
{
    uint32_t flags = spin_lock_irqsave(&pipe->lock);

    while (pipe->count == 0)
    {
        if (pipe->write_refs == 0)
        {
            // empty and no write end left: EOF
            spin_unlock_irqrestore(&pipe->lock, flags);
            return 0;
        }
        wait_queue_block_locked(&pipe->readers, &pipe->lock);
    }

    uint32_t to_read = pipe->count < count ? pipe->count : count;
    ring_get(pipe, buf, to_read);

    // freed space: let blocked writers continue
    wait_queue_wake_all(&pipe->writers);

    spin_unlock_irqrestore(&pipe->lock, flags);
    return to_read;
}

uint32_t pipe_write(pipe_buf_t *pipe, const char *buf, uint32_t count)
{
    uint32_t written = 0;

    uint32_t flags = spin_lock_irqsave(&pipe->lock);

    while (written < count)
    {
        if (pipe->read_refs == 0)
        {
            // no reader will ever drain the buffer: broken pipe
            break;
        }

        uint32_t space = PIPE_BUF_SIZE - pipe->count;
        if (space == 0)
        {
            wait_queue_block_locked(&pipe->writers, &pipe->lock);
            continue;
        }

        uint32_t chunk = count - written < space ? count - written : space;
        ring_put(pipe, buf + written, chunk);
        written += chunk;

        // data available: let blocked readers continue
        wait_queue_wake_all(&pipe->readers);
    }

    spin_unlock_irqrestore(&pipe->lock, flags);
    return written;
}

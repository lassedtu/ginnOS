#include "ktest.h"

#include "common/memory.h"
#include "kernel/syscall/pipe.h"

// tests run on the boot stack with no current process, so they only take
// paths that never block: reads with data buffered or at EOF, and writes
// that fit or hit a broken pipe.

// static: test bodies run on the boot stack, keep big buffers off it.
static char write_buf[PIPE_BUF_SIZE];
static char read_buf[PIPE_BUF_SIZE];

/**
 * close both ends of a test pipe so it returns to the pool.
 */
static void close_pipe(pipe_buf_t *pipe)
{
    pipe_unref(pipe, PIPE_READ);
    pipe_unref(pipe, PIPE_WRITE);
}

KTEST(pipe_write_then_read)
{
    pipe_buf_t *pipe = pipe_alloc();
    KEXPECT(pipe != NULL);
    if (!pipe)
        return;

    KEXPECT_EQ(pipe_write(pipe, "hello", 5), 5);
    KEXPECT_EQ(pipe->count, 5);

    // a short read leaves the rest buffered, in order
    KEXPECT_EQ(pipe_read(pipe, read_buf, 2), 2);
    KEXPECT(memcmp(read_buf, "he", 2) == 0);
    KEXPECT_EQ(pipe_read(pipe, read_buf, sizeof(read_buf)), 3);
    KEXPECT(memcmp(read_buf, "llo", 3) == 0);
    KEXPECT_EQ(pipe->count, 0);

    close_pipe(pipe);
}

KTEST(pipe_wraps_around)
{
    pipe_buf_t *pipe = pipe_alloc();
    KEXPECT(pipe != NULL);
    if (!pipe)
        return;

    for (uint32_t i = 0; i < PIPE_BUF_SIZE; i++)
        write_buf[i] = (char)(i * 13 + 1);

    // move the ring positions near the end, then fill it completely so the
    // next write and read both cross the end of the buffer
    KEXPECT_EQ(pipe_write(pipe, write_buf, PIPE_BUF_SIZE - 10), PIPE_BUF_SIZE - 10);
    KEXPECT_EQ(pipe_read(pipe, read_buf, PIPE_BUF_SIZE), PIPE_BUF_SIZE - 10);

    KEXPECT_EQ(pipe_write(pipe, write_buf, PIPE_BUF_SIZE), PIPE_BUF_SIZE);
    KEXPECT_EQ(pipe->count, PIPE_BUF_SIZE);

    memset(read_buf, 0, sizeof(read_buf));
    KEXPECT_EQ(pipe_read(pipe, read_buf, PIPE_BUF_SIZE), PIPE_BUF_SIZE);
    KEXPECT(memcmp(read_buf, write_buf, PIPE_BUF_SIZE) == 0);

    close_pipe(pipe);
}

KTEST(pipe_eof_after_last_writer_closes)
{
    pipe_buf_t *pipe = pipe_alloc();
    KEXPECT(pipe != NULL);
    if (!pipe)
        return;

    // a second write end (dup2 or an inherited fd) keeps the pipe open
    pipe_ref(pipe, PIPE_WRITE);
    KEXPECT_EQ(pipe_write(pipe, "ab", 2), 2);
    pipe_unref(pipe, PIPE_WRITE);
    pipe_unref(pipe, PIPE_WRITE);

    // buffered data is still delivered, then EOF
    KEXPECT_EQ(pipe_read(pipe, read_buf, sizeof(read_buf)), 2);
    KEXPECT_EQ(pipe_read(pipe, read_buf, sizeof(read_buf)), 0);

    pipe_unref(pipe, PIPE_READ);
}

KTEST(pipe_broken_after_last_reader_closes)
{
    pipe_buf_t *pipe = pipe_alloc();
    KEXPECT(pipe != NULL);
    if (!pipe)
        return;

    pipe_unref(pipe, PIPE_READ);
    KEXPECT_EQ(pipe_write(pipe, "x", 1), 0);

    pipe_unref(pipe, PIPE_WRITE);
}

KTEST(pipe_pool_exhausts_and_reuses)
{
    pipe_buf_t *pipes[PIPE_MAX];
    uint32_t allocated = 0;

    // other tests closed their pipes, so the whole pool is free
    while (allocated < PIPE_MAX)
    {
        pipes[allocated] = pipe_alloc();
        if (!pipes[allocated])
            break;
        allocated++;
    }
    KEXPECT_EQ(allocated, PIPE_MAX);
    KEXPECT(pipe_alloc() == NULL);

    // a pipe returns to the pool only once both ends are closed
    pipe_unref(pipes[0], PIPE_READ);
    KEXPECT(pipe_alloc() == NULL);
    pipe_unref(pipes[0], PIPE_WRITE);

    pipe_buf_t *reused = pipe_alloc();
    KEXPECT(reused == pipes[0]);
    if (reused)
    {
        // a reused pipe starts empty with both ends open
        KEXPECT_EQ(reused->count, 0);
        KEXPECT_EQ(reused->read_refs, 1);
        KEXPECT_EQ(reused->write_refs, 1);
    }

    for (uint32_t i = 0; i < allocated; i++)
        close_pipe(pipes[i]);
}

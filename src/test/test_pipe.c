#include "ktest.h"

#include "common/memory.h"
#include "kernel/syscall/pipe.h"
#include "kernel/memory/heap.h"
#include "kernel/errno.h"

// tests run on the boot stack with no current process, so they only take
// paths that never block: reads with data buffered or at EOF, and writes
// that fit or hit a broken pipe.

// static: test bodies run on the boot stack, keep big buffers off it.
static char write_buf[PIPE_CAPACITY];
static char read_buf[PIPE_CAPACITY];

/**
 * close both ends of a test pipe, which frees it.
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

    KEXPECT_EQ(pipe_write(pipe, "hello", 5, false), 5);
    KEXPECT_EQ(pipe->count, 5);

    // a short read leaves the rest buffered, in order
    KEXPECT_EQ(pipe_read(pipe, read_buf, 2, false), 2);
    KEXPECT(memcmp(read_buf, "he", 2) == 0);
    KEXPECT_EQ(pipe_read(pipe, read_buf, sizeof(read_buf), false), 3);
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

    for (uint32_t i = 0; i < PIPE_CAPACITY; i++)
        write_buf[i] = (char)(i * 13 + 1);

    // move the ring positions near the end, then fill it completely so the
    // next write and read both cross the end of the buffer
    KEXPECT_EQ(pipe_write(pipe, write_buf, PIPE_CAPACITY - 10, false), PIPE_CAPACITY - 10);
    KEXPECT_EQ(pipe_read(pipe, read_buf, PIPE_CAPACITY, false), PIPE_CAPACITY - 10);

    KEXPECT_EQ(pipe_write(pipe, write_buf, PIPE_CAPACITY, false), PIPE_CAPACITY);
    KEXPECT_EQ(pipe->count, PIPE_CAPACITY);

    memset(read_buf, 0, sizeof(read_buf));
    KEXPECT_EQ(pipe_read(pipe, read_buf, PIPE_CAPACITY, false), PIPE_CAPACITY);
    KEXPECT(memcmp(read_buf, write_buf, PIPE_CAPACITY) == 0);

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
    KEXPECT_EQ(pipe_write(pipe, "ab", 2, false), 2);
    pipe_unref(pipe, PIPE_WRITE);
    pipe_unref(pipe, PIPE_WRITE);

    // buffered data is still delivered, then EOF
    KEXPECT_EQ(pipe_read(pipe, read_buf, sizeof(read_buf), false), 2);
    KEXPECT_EQ(pipe_read(pipe, read_buf, sizeof(read_buf), false), 0);

    pipe_unref(pipe, PIPE_READ);
}

KTEST(pipe_broken_after_last_reader_closes)
{
    pipe_buf_t *pipe = pipe_alloc();
    KEXPECT(pipe != NULL);
    if (!pipe)
        return;

    pipe_unref(pipe, PIPE_READ);
    KEXPECT_EQ(pipe_write(pipe, "x", 1, false), -EPIPE);

    pipe_unref(pipe, PIPE_WRITE);
}

KTEST(pipe_nonblocking_returns_eagain)
{
    pipe_buf_t *pipe = pipe_alloc();
    KEXPECT(pipe != NULL);
    if (!pipe)
        return;

    // empty, but a writer is open: no EOF, nothing to read yet
    KEXPECT_EQ(pipe_read(pipe, read_buf, 1, true), -EAGAIN);

    // full: nothing fits; a partly fitting write takes what fits
    KEXPECT_EQ(pipe_write(pipe, write_buf, PIPE_CAPACITY - 1, true), PIPE_CAPACITY - 1);
    KEXPECT_EQ(pipe_write(pipe, write_buf, PIPE_BUF + 1, true), 1);
    KEXPECT_EQ(pipe_write(pipe, write_buf, 1, true), -EAGAIN);

    close_pipe(pipe);
}

KTEST(pipes_are_not_limited_to_a_pool)
{
    // more pipes than the old fixed pool of 16 had
    pipe_buf_t *pipes[64];
    uint32_t allocated = 0;
    heap_stats_t before = heap_get_stats();

    while (allocated < 64)
    {
        pipes[allocated] = pipe_alloc();
        if (!pipes[allocated])
            break;
        allocated++;
    }
    KEXPECT_EQ(allocated, 64);

    // a pipe is freed only once both ends are closed
    for (uint32_t i = 0; i < allocated; i++)
        close_pipe(pipes[i]);

    heap_stats_t after = heap_get_stats();
    KEXPECT_EQ(after.current_usage, before.current_usage);
}

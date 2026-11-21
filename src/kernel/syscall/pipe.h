#pragma once

/**
 * pipes: a ring buffer shared by a read end and a write end. each pipe and
 * its buffer come from the kernel heap, so the number of pipes is limited
 * only by memory (and by the fds that hold their ends).
 *
 * a write of at most PIPE_BUF bytes is atomic: it waits until there is room
 * for all of it, so its bytes are never mixed with another writer's. larger
 * writes can be split.
 *
 * every field of a pipe is guarded by its spinlock, so reads, writes and
 * reference changes are atomic against preemption (syscalls run with
 * interrupts enabled). readers and writers that must wait sleep on the
 * pipe's wait queues with wait_queue_block_locked(), which releases the
 * lock atomically, so a wakeup between the check and the sleep is never
 * lost.
 */

#include "common/stdint.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"

// bytes a pipe can hold before a writer waits
#define PIPE_CAPACITY 16384

// largest write that is never split (POSIX PIPE_BUF; also in libc limits.h)
#define PIPE_BUF 4096

/**
 * pipe endpoint direction.
 */
typedef enum
{
    PIPE_READ = 0,
    PIPE_WRITE = 1,
} pipe_dir_t;

/**
 * a pipe. shared between all of its read and write ends.
 */
typedef struct pipe_buf
{
    char *data;          // PIPE_CAPACITY bytes from the heap
    uint32_t read_pos;   // next byte to read
    uint32_t write_pos;  // next byte to write
    uint32_t count;      // bytes currently in buffer
    int32_t read_refs;   // number of open read ends
    int32_t write_refs;  // number of open write ends
    spinlock_t lock;     // guards every field above

    wait_queue_t readers; // readers blocked waiting for data
    wait_queue_t writers; // writers blocked waiting for free space
} pipe_buf_t;

/**
 * allocate a pipe with one open read end and one open write end.
 * @return the new pipe, or NULL if the heap is out of memory.
 */
pipe_buf_t *pipe_alloc(void);

/**
 * open another end of a pipe (dup2 or fd inheritance).
 * @param pipe the pipe.
 * @param dir which end the new reference is.
 */
void pipe_ref(pipe_buf_t *pipe, pipe_dir_t dir);

/**
 * close one end of a pipe. closing the last read end wakes blocked writers
 * (they then see a broken pipe); closing the last write end wakes blocked
 * readers (they then see EOF). the pipe is freed once both ends are fully
 * closed.
 * @param pipe the pipe.
 * @param dir which end is being closed.
 */
void pipe_unref(pipe_buf_t *pipe, pipe_dir_t dir);

/**
 * read from a pipe, sleeping while it is empty and a write end is open.
 * @param pipe the pipe.
 * @param buf destination buffer.
 * @param count maximum number of bytes to read (> 0).
 * @param nonblock return -EAGAIN instead of sleeping.
 * @return bytes read; 0 only at EOF (empty and no write end open); or
 *         -EAGAIN.
 */
int32_t pipe_read(pipe_buf_t *pipe, char *buf, uint32_t count, bool nonblock);

/**
 * write every byte to a pipe, sleeping whenever it is full. a write of at
 * most PIPE_BUF bytes goes in as one piece.
 * @param pipe the pipe.
 * @param buf source buffer.
 * @param count number of bytes to write (> 0).
 * @param nonblock stop instead of sleeping when the pipe is full.
 * @return bytes written (less than count if every read end closed, or the
 *         pipe filled up in nonblocking mode); -EPIPE if no read end is
 *         open; -EAGAIN if nonblocking and nothing fitted.
 */
int32_t pipe_write(pipe_buf_t *pipe, const char *buf, uint32_t count, bool nonblock);

/**
 * the readiness of one end of a pipe: the read end has POLLIN when there
 * is data or no writer is left (POLLHUP then too); the write end has
 * POLLOUT when PIPE_BUF bytes fit, and POLLERR when no reader is left.
 * @param pipe the pipe.
 * @param dir which end.
 * @return POLL* events (kernel/vfs/poll.h).
 */
uint32_t pipe_poll(pipe_buf_t *pipe, pipe_dir_t dir);

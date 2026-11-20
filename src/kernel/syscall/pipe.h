#pragma once

/**
 * pipes: a fixed-size ring buffer shared by a read end and a write end.
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

// pipe buffer size (4 KiB)
#define PIPE_BUF_SIZE 4096

// maximum number of simultaneous pipes system-wide
#define PIPE_MAX 16

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
    char data[PIPE_BUF_SIZE];
    uint32_t read_pos;   // next byte to read
    uint32_t write_pos;  // next byte to write
    uint32_t count;      // bytes currently in buffer
    int32_t read_refs;   // number of open read ends
    int32_t write_refs;  // number of open write ends
    spinlock_t lock;     // guards every field above
    bool in_use;         // allocated from the pool (guarded by the pool lock)

    wait_queue_t readers; // readers blocked waiting for data
    wait_queue_t writers; // writers blocked waiting for free space
} pipe_buf_t;

/**
 * initialize the system pipe pool. every pipe starts free.
 */
void pipe_init(void);

/**
 * allocate a pipe from the system pool, with one open read end and one
 * open write end.
 * @return the new pipe, or NULL if the pool is exhausted.
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
 * readers (they then see EOF). the pipe returns to the pool once both ends
 * are fully closed.
 * @param pipe the pipe.
 * @param dir which end is being closed.
 */
void pipe_unref(pipe_buf_t *pipe, pipe_dir_t dir);

/**
 * read from a pipe, sleeping while it is empty and a write end is open.
 * @param pipe the pipe.
 * @param buf destination buffer.
 * @param count maximum number of bytes to read (> 0).
 * @return bytes read; 0 only at EOF (empty and no write end open).
 */
uint32_t pipe_read(pipe_buf_t *pipe, char *buf, uint32_t count);

/**
 * write every byte to a pipe, sleeping whenever it is full.
 * @param pipe the pipe.
 * @param buf source buffer.
 * @param count number of bytes to write.
 * @return bytes written; less than count only if every read end closed.
 */
uint32_t pipe_write(pipe_buf_t *pipe, const char *buf, uint32_t count);

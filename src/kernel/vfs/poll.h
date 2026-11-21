#pragma once

/**
 * readiness of open files, for poll and select.
 *
 * each kind of file reports its readiness through file_ops_t::poll. a
 * process in poll sleeps on one shared queue; whatever can change some
 * file's readiness (pipe reads, writes and closes, a key press) calls
 * poll_notify, and every poller checks its files again.
 */

#include "common/stdint.h"

// poll events (ABI, as in libc poll.h)
#define POLLIN   0x001 // data to read (or EOF)
#define POLLPRI  0x002 // urgent data (never set)
#define POLLOUT  0x004 // room to write
#define POLLERR  0x008 // an error (a pipe with no reader left)
#define POLLHUP  0x010 // hung up (a pipe with no writer left)
#define POLLNVAL 0x020 // not an open descriptor

/**
 * set up the poll queue. call once at boot, before the first poll.
 */
void poll_init(void);

/**
 * wake every process in poll: some file's readiness may have changed.
 * safe to call from an interrupt handler.
 */
void poll_notify(void);

/**
 * sleep until poll_notify or the deadline tick. interrupts must be masked
 * from the readiness check to here.
 * @param deadline the tick to wake at, or TIMER_NEVER.
 */
void poll_wait(uint64_t deadline);

#pragma once

/**
 * @file poll.h
 * @brief waiting for file descriptors to become ready.
 *
 * the event bits and struct pollfd are ABI and must match the kernel side
 * in src/kernel/vfs/poll.h and sys_io.c.
 */

#define POLLIN   0x001 // data to read (or end of file)
#define POLLPRI  0x002 // urgent data (never set)
#define POLLOUT  0x004 // room to write
#define POLLERR  0x008 // an error (a pipe with no reader left)
#define POLLHUP  0x010 // hung up (a pipe with no writer left)
#define POLLNVAL 0x020 // not an open descriptor

typedef unsigned int nfds_t;

struct pollfd
{
    int fd;        // descriptor to watch (negative: ignored)
    short events;  // events to watch for
    short revents; // events that happened (set by poll)
};

/**
 * wait until one of a set of descriptors is ready.
 * @param fds the descriptors (at most 64).
 * @param nfds the number of entries.
 * @param timeout milliseconds to wait; negative waits without limit, 0
 *        does not wait.
 * @return the number of entries with revents set, 0 on timeout, or -1
 *         (errno set).
 */
int poll(struct pollfd *fds, nfds_t nfds, int timeout);

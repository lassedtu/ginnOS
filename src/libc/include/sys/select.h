#pragma once

/**
 * @file sys/select.h
 * @brief select(), built on poll().
 */

// descriptors an fd_set can hold (more than a process can have open)
#define FD_SETSIZE 64

typedef struct
{
    unsigned int bits[FD_SETSIZE / 32];
} fd_set;

#define FD_ZERO(set)                                                \
    do                                                              \
    {                                                               \
        for (int fd_zero_i = 0; fd_zero_i < FD_SETSIZE / 32; fd_zero_i++) \
            (set)->bits[fd_zero_i] = 0;                             \
    } while (0)
#define FD_SET(fd, set)   ((set)->bits[(fd) / 32] |= 1u << ((fd) % 32))
#define FD_CLR(fd, set)   ((set)->bits[(fd) / 32] &= ~(1u << ((fd) % 32)))
#define FD_ISSET(fd, set) (((set)->bits[(fd) / 32] >> ((fd) % 32)) & 1u)

#include <sys/time.h>

/**
 * wait until a descriptor in one of the sets is ready. on return each set
 * holds only its ready descriptors.
 * @param nfds one more than the highest descriptor in the sets.
 * @param readfds descriptors to watch for reading (may be NULL).
 * @param writefds descriptors to watch for writing (may be NULL).
 * @param exceptfds descriptors to watch for errors (may be NULL).
 * @param timeout how long to wait (NULL: no limit).
 * @return the number of ready descriptors in all sets, 0 on timeout, or -1
 *         (errno set).
 */
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout);

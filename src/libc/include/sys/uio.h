#pragma once

#include <sys/types.h>

/**
 * @file sys/uio.h
 * @brief struct iovec: one piece of a scattered buffer (sendmsg, recvmsg).
 *
 * the layout is ABI and must match kiovec_t in
 * src/kernel/ipc/unix_socket.h.
 */

struct iovec
{
    void *iov_base;
    size_t iov_len;
};

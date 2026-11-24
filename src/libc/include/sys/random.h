#pragma once

#include <sys/types.h>

/**
 * @file sys/random.h
 * @brief random bytes from the kernel's generator (getrandom).
 */

#define GRND_NONBLOCK 0x0001 // do not wait for the generator to be seeded
#define GRND_RANDOM   0x0002 // the same as without (as /dev/random)
#define GRND_INSECURE 0x0004 // do not wait; the bytes may be weak early at boot

/**
 * fill buf with length random bytes. waits until the kernel's generator is
 * seeded (early at boot only), unless GRND_NONBLOCK or GRND_INSECURE.
 * @return the bytes given (a long request can stop early at a signal), or
 *         -1 with errno EAGAIN, EINTR, EINVAL or EFAULT.
 */
ssize_t getrandom(void *buf, size_t length, unsigned int flags);

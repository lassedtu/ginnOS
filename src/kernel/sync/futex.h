#pragma once

/**
 * futex: a wait on a word in user memory. a thread sleeps while the word
 * has the value it expects (FUTEX_WAIT), and another thread wakes it
 * (FUTEX_WAKE) after it changes the word. libc builds its mutexes and
 * condition variables on this, and pthread_join on the CLEARTID wake.
 *
 * a futex is known by its address space and its user address, so only
 * the threads of one process share a futex (FUTEX_PRIVATE_FLAG on
 * Linux). there is no futex between processes in shared memory yet.
 */

#include "common/stdint.h"

struct user_space;

/**
 * set up the queues. call once at boot, before the first futex call.
 */
void futex_init(void);

/**
 * wake threads that sleep on a futex.
 * @param space the address space of the futex.
 * @param uaddr its user address.
 * @param count the most threads to wake.
 * @return the number of threads woken.
 */
int futex_wake(struct user_space *space, uintptr_t uaddr, int count);

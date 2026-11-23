/**
 * @file pthread_mutex.c
 * @brief mutexes and spin locks (pthread.h).
 *
 * the lock word is 0 (free), 1 (locked) or 2 (locked, and a thread may
 * sleep on it). a thread that finds it locked sets it to 2 and sleeps on
 * the futex; an unlock that finds 2 wakes one sleeper. ("Futexes Are
 * Tricky", Ulrich Drepper, mutex 2.)
 */

#include "thread.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>

static int my_tid(void)
{
    return thread_self()->tid;
}

/**
 * take the lock word.
 * @param abstime the time to give up at (CLOCK_REALTIME), or NULL.
 * @return 0 or ETIMEDOUT.
 */
static int take(volatile int *word, const struct timespec *abstime)
{
    int c = 0;
    if (__atomic_compare_exchange_n(word, &c, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        return 0;
    }
    if (c != 2)
    {
        c = __atomic_exchange_n(word, 2, __ATOMIC_ACQUIRE);
    }
    while (c != 0)
    {
        if (abstime)
        {
            struct timespec left;
            int err = __time_left(CLOCK_REALTIME, abstime, &left);
            if (err)
            {
                return err;
            }
            __futex_wait(word, 2, &left);
        }
        else
        {
            __futex_wait(word, 2, NULL);
        }
        c = __atomic_exchange_n(word, 2, __ATOMIC_ACQUIRE);
    }
    return 0;
}

/**
 * what a lock of the recursive and error-check types does when the
 * caller holds the mutex already.
 * @return -1 when the caller does not hold it, else the result.
 */
static int locked_by_me(pthread_mutex_t *mutex)
{
    if (mutex->type == PTHREAD_MUTEX_NORMAL || mutex->owner != my_tid())
    {
        return -1;
    }
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK)
    {
        return EDEADLK;
    }
    if (mutex->count == 0x7FFFFFFF)
    {
        return EAGAIN;
    }
    mutex->count++;
    return 0;
}

static int lock(pthread_mutex_t *mutex, const struct timespec *abstime)
{
    int mine = locked_by_me(mutex);
    if (mine >= 0)
    {
        return mine;
    }
    int err = take(&mutex->lock, abstime);
    if (err == 0)
    {
        mutex->owner = my_tid();
        mutex->count = 1;
    }
    return err;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    return lock(mutex, NULL);
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime)
{
    return lock(mutex, abstime);
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    int mine = locked_by_me(mutex);
    if (mine >= 0)
    {
        return mine == EDEADLK ? EBUSY : mine;
    }
    int c = 0;
    if (!__atomic_compare_exchange_n(&mutex->lock, &c, 1, false, __ATOMIC_ACQUIRE,
                                     __ATOMIC_RELAXED))
    {
        return EBUSY;
    }
    mutex->owner = my_tid();
    mutex->count = 1;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (mutex->type != PTHREAD_MUTEX_NORMAL)
    {
        if (mutex->owner != my_tid() || mutex->lock == 0)
        {
            return EPERM;
        }
        if (--mutex->count > 0)
        {
            return 0;
        }
    }
    mutex->owner = 0;
    if (__atomic_exchange_n(&mutex->lock, 0, __ATOMIC_RELEASE) == 2)
    {
        __futex_wake(&mutex->lock, 1);
    }
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    *mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    mutex->type = attr ? attr->type : PTHREAD_MUTEX_DEFAULT;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    return mutex->lock ? EBUSY : 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    attr->type = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    if (type < PTHREAD_MUTEX_NORMAL || type > PTHREAD_MUTEX_ERRORCHECK)
    {
        return EINVAL;
    }
    attr->type = type;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
    *type = attr->type;
    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int shared)
{
    (void)attr;
    return shared == PTHREAD_PROCESS_PRIVATE ? 0 : ENOTSUP;
}

int pthread_spin_init(pthread_spinlock_t *lock, int shared)
{
    (void)shared;
    *lock = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock)
{
    (void)lock;
    return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock)
{
    // one CPU: a thread that spins keeps the holder from running, so
    // give the CPU away while the lock is taken
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE))
    {
        sched_yield();
    }
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock)
{
    return __atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE) ? EBUSY : 0;
}

int pthread_spin_unlock(pthread_spinlock_t *lock)
{
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
    return 0;
}

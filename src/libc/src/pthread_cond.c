/**
 * @file pthread_cond.c
 * @brief condition variables, and the read-write locks and barriers made
 *        of them (pthread.h).
 *
 * a condition variable is a sequence number. a waiter reads it, lets go
 * of the mutex and sleeps while the number has not changed; a signal or
 * broadcast changes it and wakes one or every sleeper. a signal that
 * comes between the read and the sleep changes the number, so the sleep
 * does not start and the wake is not lost. a wait may end without a
 * signal, which POSIX allows: callers test their condition in a loop.
 */

#include "thread.h"

#include <errno.h>
#include <pthread.h>

static int wait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
    struct timespec left;
    if (abstime)
    {
        int err = __time_left(cond->clock, abstime, &left);
        if (err)
        {
            return err;
        }
    }

    int sequence = cond->sequence;

    // a recursive mutex is let go of completely, and taken back as deep
    int depth = mutex->count;
    mutex->count = 1;
    pthread_mutex_unlock(mutex);

    int ret = __futex_wait(&cond->sequence, sequence, abstime ? &left : NULL);

    pthread_mutex_lock(mutex);
    mutex->count = depth;
    return ret == -ETIMEDOUT ? ETIMEDOUT : 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return wait(cond, mutex, NULL);
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime)
{
    return wait(cond, mutex, abstime);
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    __atomic_add_fetch(&cond->sequence, 1, __ATOMIC_SEQ_CST);
    __futex_wake(&cond->sequence, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    __atomic_add_fetch(&cond->sequence, 1, __ATOMIC_SEQ_CST);
    __futex_wake(&cond->sequence, 0x7FFFFFFF);
    return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    cond->sequence = 0;
    cond->clock = attr ? attr->clock : CLOCK_REALTIME;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_condattr_init(pthread_condattr_t *attr)
{
    attr->clock = CLOCK_REALTIME;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock)
{
    if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)
    {
        return EINVAL;
    }
    attr->clock = clock;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *attr, clockid_t *clock)
{
    *clock = attr->clock;
    return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *attr, int shared)
{
    (void)attr;
    return shared == PTHREAD_PROCESS_PRIVATE ? 0 : ENOTSUP;
}

// read-write locks. a writer that waits goes before readers that come
// after it, so a stream of readers cannot keep writers out.

int pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr)
{
    (void)attr;
    *lock = (pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock)
{
    return lock->readers || lock->writer ? EBUSY : 0;
}

static int read_lock(pthread_rwlock_t *lock, const struct timespec *abstime, int try)
{
    int err = 0;
    pthread_mutex_lock(&lock->mutex);
    while (lock->writer || lock->writers_waiting)
    {
        if (try)
        {
            err = EBUSY;
            break;
        }
        err = wait(&lock->readers_go, &lock->mutex, abstime);
        if (err)
        {
            break;
        }
    }
    if (!err)
    {
        lock->readers++;
    }
    pthread_mutex_unlock(&lock->mutex);
    return err;
}

static int write_lock(pthread_rwlock_t *lock, const struct timespec *abstime, int try)
{
    int err = 0;
    pthread_mutex_lock(&lock->mutex);
    lock->writers_waiting++;
    while (lock->writer || lock->readers)
    {
        if (try)
        {
            err = EBUSY;
            break;
        }
        err = wait(&lock->writers_go, &lock->mutex, abstime);
        if (err)
        {
            break;
        }
    }
    lock->writers_waiting--;
    if (!err)
    {
        lock->writer = 1;
    }
    else if (!lock->writers_waiting)
    {
        // the readers that waited for this writer may go
        pthread_cond_broadcast(&lock->readers_go);
    }
    pthread_mutex_unlock(&lock->mutex);
    return err;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *lock)
{
    return read_lock(lock, NULL, 0);
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock)
{
    return read_lock(lock, NULL, 1);
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *abstime)
{
    return read_lock(lock, abstime, 0);
}

int pthread_rwlock_wrlock(pthread_rwlock_t *lock)
{
    return write_lock(lock, NULL, 0);
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *lock)
{
    return write_lock(lock, NULL, 1);
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *abstime)
{
    return write_lock(lock, abstime, 0);
}

int pthread_rwlock_unlock(pthread_rwlock_t *lock)
{
    pthread_mutex_lock(&lock->mutex);
    if (lock->writer)
    {
        lock->writer = 0;
    }
    else if (lock->readers > 0)
    {
        lock->readers--;
    }
    else
    {
        pthread_mutex_unlock(&lock->mutex);
        return EPERM;
    }
    if (!lock->writer && lock->readers == 0)
    {
        if (lock->writers_waiting)
        {
            pthread_cond_signal(&lock->writers_go);
        }
        else
        {
            pthread_cond_broadcast(&lock->readers_go);
        }
    }
    pthread_mutex_unlock(&lock->mutex);
    return 0;
}

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr)
{
    attr->unused = 0;
    return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr)
{
    (void)attr;
    return 0;
}

// barriers: the last of count threads to arrive starts a new round and
// wakes the others.

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr,
                         unsigned count)
{
    (void)attr;
    if (count == 0)
    {
        return EINVAL;
    }
    *barrier = (pthread_barrier_t){0};
    barrier->count = count;
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    return barrier->waiting ? EBUSY : 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
    pthread_mutex_lock(&barrier->mutex);
    unsigned round = barrier->round;
    if (++barrier->waiting == barrier->count)
    {
        barrier->waiting = 0;
        barrier->round++;
        pthread_cond_broadcast(&barrier->all_in);
        pthread_mutex_unlock(&barrier->mutex);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    while (round == barrier->round)
    {
        pthread_cond_wait(&barrier->all_in, &barrier->mutex);
    }
    pthread_mutex_unlock(&barrier->mutex);
    return 0;
}

int pthread_barrierattr_init(pthread_barrierattr_t *attr)
{
    attr->unused = 0;
    return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t *attr)
{
    (void)attr;
    return 0;
}

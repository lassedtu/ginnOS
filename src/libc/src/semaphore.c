/**
 * @file semaphore.c
 * @brief unnamed semaphores (semaphore.h). the value is the futex word:
 *        a waiter sleeps while it is 0, and a post wakes one waiter.
 */

#include "thread.h"

#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>

int sem_init(sem_t *sem, int pshared, unsigned value)
{
    if (value > SEM_VALUE_MAX)
    {
        errno = EINVAL;
        return -1;
    }
    if (pshared)
    {
        errno = ENOSYS; // a futex is private to one process
        return -1;
    }
    sem->value = (int)value;
    sem->waiters = 0;
    return 0;
}

int sem_destroy(sem_t *sem)
{
    (void)sem;
    return 0;
}

/** take one from the value if it is above 0 */
static bool take(sem_t *sem)
{
    int value = __atomic_load_n(&sem->value, __ATOMIC_RELAXED);
    while (value > 0)
    {
        if (__atomic_compare_exchange_n(&sem->value, &value, value - 1, false, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED))
        {
            return true;
        }
    }
    return false;
}

static int wait(sem_t *sem, const struct timespec *abstime)
{
    while (!take(sem))
    {
        struct timespec left;
        if (abstime)
        {
            int err = __time_left(CLOCK_REALTIME, abstime, &left);
            if (err)
            {
                errno = err;
                return -1;
            }
        }
        __atomic_add_fetch(&sem->waiters, 1, __ATOMIC_SEQ_CST);
        int ret = __futex_wait(&sem->value, 0, abstime ? &left : NULL);
        __atomic_sub_fetch(&sem->waiters, 1, __ATOMIC_SEQ_CST);
        if (ret == -EINTR)
        {
            errno = EINTR;
            return -1;
        }
    }
    return 0;
}

int sem_wait(sem_t *sem)
{
    return wait(sem, NULL);
}

int sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
    return wait(sem, abstime);
}

int sem_trywait(sem_t *sem)
{
    if (take(sem))
    {
        return 0;
    }
    errno = EAGAIN;
    return -1;
}

int sem_post(sem_t *sem)
{
    int value = __atomic_load_n(&sem->value, __ATOMIC_RELAXED);
    do
    {
        if (value == SEM_VALUE_MAX)
        {
            errno = EOVERFLOW;
            return -1;
        }
    } while (!__atomic_compare_exchange_n(&sem->value, &value, value + 1, false,
                                          __ATOMIC_RELEASE, __ATOMIC_RELAXED));
    if (__atomic_load_n(&sem->waiters, __ATOMIC_SEQ_CST))
    {
        __futex_wake(&sem->value, 1);
    }
    return 0;
}

int sem_getvalue(sem_t *sem, int *value)
{
    *value = sem->value;
    return 0;
}

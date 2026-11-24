/**
 * @file threads.c
 * @brief libctest: the calls of pthread.h and semaphore.h, as one thread
 *        and with one more (pthreadtest tests many threads at once).
 */

#include "libctest.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <string.h>

static int once_count;

static void count_once(void)
{
    once_count++;
}

static void *add_one(void *arg)
{
    return (void *)((intptr_t)arg + 1);
}

static void *read_errno(void *arg)
{
    (void)arg;
    return (void *)(intptr_t)errno;
}

void test_threads(void)
{
    // attributes
    pthread_attr_t attr;
    size_t size = 0;
    int state = -1;
    CHECK(pthread_attr_init(&attr) == 0);
    CHECK(pthread_attr_getstacksize(&attr, &size) == 0 && size >= PTHREAD_STACK_MIN);
    CHECK(pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN - 1) == EINVAL);
    CHECK(pthread_attr_setstacksize(&attr, 128 * 1024) == 0);
    CHECK(pthread_attr_getdetachstate(&attr, &state) == 0 && state == PTHREAD_CREATE_JOINABLE);
    CHECK(pthread_attr_setdetachstate(&attr, 7) == EINVAL);

    // a thread, its result, and an errno of its own
    pthread_t thread;
    void *result = NULL;
    CHECK(pthread_create(&thread, &attr, add_one, (void *)41) == 0);
    CHECK(pthread_join(thread, &result) == 0 && (intptr_t)result == 42);
    errno = ERANGE;
    CHECK(pthread_create(&thread, NULL, read_errno, NULL) == 0);
    CHECK(pthread_join(thread, &result) == 0 && (intptr_t)result == 0 && errno == ERANGE);
    CHECK(pthread_equal(pthread_self(), pthread_self()));
    CHECK(pthread_join(pthread_self(), NULL) == EDEADLK);

    // mutexes
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    CHECK(pthread_mutex_lock(&mutex) == 0);
    CHECK(pthread_mutex_trylock(&mutex) == EBUSY);
    CHECK(pthread_mutex_destroy(&mutex) == EBUSY);
    CHECK(pthread_mutex_unlock(&mutex) == 0);
    CHECK(pthread_mutex_destroy(&mutex) == 0);
    pthread_mutexattr_t mattr;
    int type = -1;
    pthread_mutexattr_init(&mattr);
    CHECK(pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE) == 0);
    CHECK(pthread_mutexattr_gettype(&mattr, &type) == 0 && type == PTHREAD_MUTEX_RECURSIVE);
    CHECK(pthread_mutexattr_settype(&mattr, 9) == EINVAL);
    CHECK(pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED) == ENOTSUP);
    pthread_mutex_init(&mutex, &mattr);
    CHECK(pthread_mutex_lock(&mutex) == 0 && pthread_mutex_trylock(&mutex) == 0);
    CHECK(pthread_mutex_unlock(&mutex) == 0 && pthread_mutex_unlock(&mutex) == 0);
    CHECK(pthread_mutex_unlock(&mutex) == EPERM);

    // a timed wait with nothing to wake it
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    struct timespec past = {0, 0};
    pthread_mutex_lock(&mutex);
    CHECK(pthread_cond_timedwait(&cond, &mutex, &past) == ETIMEDOUT);
    pthread_mutex_unlock(&mutex);
    CHECK(pthread_cond_signal(&cond) == 0 && pthread_cond_broadcast(&cond) == 0);
    pthread_condattr_t cattr;
    clockid_t clock = -1;
    pthread_condattr_init(&cattr);
    CHECK(pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC) == 0);
    CHECK(pthread_condattr_getclock(&cattr, &clock) == 0 && clock == CLOCK_MONOTONIC);
    CHECK(pthread_condattr_setclock(&cattr, CLOCK_PROCESS_CPUTIME_ID) == EINVAL);

    // once and keys
    pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, count_once);
    pthread_once(&once, count_once);
    CHECK(once_count == 1);
    pthread_key_t key;
    CHECK(pthread_key_create(&key, NULL) == 0);
    CHECK(pthread_getspecific(key) == NULL);
    CHECK(pthread_setspecific(key, &once_count) == 0 && pthread_getspecific(key) == &once_count);
    CHECK(pthread_key_delete(key) == 0);
    CHECK(pthread_setspecific(key, &once_count) == EINVAL);
    CHECK(pthread_key_delete(PTHREAD_KEYS_MAX) == EINVAL);

    // read-write locks, barriers and spin locks
    pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
    CHECK(pthread_rwlock_rdlock(&rwlock) == 0 && pthread_rwlock_tryrdlock(&rwlock) == 0);
    CHECK(pthread_rwlock_trywrlock(&rwlock) == EBUSY);
    CHECK(pthread_rwlock_unlock(&rwlock) == 0 && pthread_rwlock_unlock(&rwlock) == 0);
    CHECK(pthread_rwlock_unlock(&rwlock) == EPERM);
    CHECK(pthread_rwlock_wrlock(&rwlock) == 0 && pthread_rwlock_tryrdlock(&rwlock) == EBUSY);
    CHECK(pthread_rwlock_unlock(&rwlock) == 0);
    pthread_barrier_t barrier;
    CHECK(pthread_barrier_init(&barrier, NULL, 0) == EINVAL);
    CHECK(pthread_barrier_init(&barrier, NULL, 1) == 0);
    CHECK(pthread_barrier_wait(&barrier) == PTHREAD_BARRIER_SERIAL_THREAD);
    pthread_spinlock_t spin;
    pthread_spin_init(&spin, PTHREAD_PROCESS_PRIVATE);
    CHECK(pthread_spin_lock(&spin) == 0 && pthread_spin_trylock(&spin) == EBUSY);
    CHECK(pthread_spin_unlock(&spin) == 0 && pthread_spin_trylock(&spin) == 0);

    // semaphores
    sem_t sem;
    int value = -1;
    CHECK(sem_init(&sem, 0, 2) == 0);
    CHECK(sem_wait(&sem) == 0 && sem_trywait(&sem) == 0);
    CHECK(sem_trywait(&sem) == -1 && errno == EAGAIN);
    CHECK(sem_timedwait(&sem, &past) == -1 && errno == ETIMEDOUT);
    CHECK(sem_post(&sem) == 0 && sem_getvalue(&sem, &value) == 0 && value == 1);
    CHECK(sem_init(&sem, 1, 0) == -1 && errno == ENOSYS);

    CHECK(strcmp(strerror(EDEADLK), "resource deadlock would occur") == 0);

    // scheduling: one policy, SCHED_OTHER at priority 0
    int policy = -1;
    struct sched_param param = {.sched_priority = 5};
    CHECK(pthread_getschedparam(pthread_self(), &policy, &param) == 0);
    CHECK(policy == SCHED_OTHER && param.sched_priority == 0);
    CHECK(pthread_setschedparam(pthread_self(), SCHED_OTHER, &param) == 0);
    CHECK(pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == EPERM);
    CHECK(sched_get_priority_min(SCHED_OTHER) == 0 && sched_get_priority_max(SCHED_OTHER) == 0);
    CHECK(sched_get_priority_min(SCHED_RR) == 1 && sched_get_priority_max(SCHED_RR) == 99);
    errno = 0;
    CHECK(sched_get_priority_max(42) == -1 && errno == EINVAL);
}

#pragma once

/**
 * @file pthread.h
 * @brief POSIX threads: start, wait for and end threads; mutexes,
 *        condition variables, read-write locks, barriers, spin locks,
 *        one-time initialization and thread-specific data.
 *
 * each thread is a kernel thread (clone) with its own stack and
 * thread-local storage. the locks sleep on futexes, which are private to
 * the process: PTHREAD_PROCESS_SHARED is not supported. there is no
 * cancellation.
 *
 * the functions return 0 or an error number; they do not set errno.
 * every object of these types is valid when it is all zero bytes, so the
 * static initializers are {0}.
 */

#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <time.h>

typedef unsigned long pthread_t;

typedef struct
{
    int detach_state;
    size_t stack_size;
    size_t guard_size;
} pthread_attr_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED  1

typedef struct
{
    volatile int lock; // 0 free, 1 locked, 2 locked with threads asleep
    int type;
    volatile int owner; // the thread id, for the recursive and error-check types
    int count;          // the times a recursive mutex is locked
} pthread_mutex_t;

typedef struct
{
    int type;
} pthread_mutexattr_t;

#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_NORMAL

#define PTHREAD_MUTEX_INITIALIZER {0}

typedef struct
{
    volatile int sequence; // changes with each signal and broadcast
    int clock;             // the clock of a timed wait
} pthread_cond_t;

typedef struct
{
    int clock;
} pthread_condattr_t;

#define PTHREAD_COND_INITIALIZER {0}

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t readers_go;
    pthread_cond_t writers_go;
    int readers;         // the threads that hold it to read
    int writer;          // 1 while a thread holds it to write
    int writers_waiting; // readers wait for these first
} pthread_rwlock_t;

typedef struct
{
    int unused;
} pthread_rwlockattr_t;

#define PTHREAD_RWLOCK_INITIALIZER {0}

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t all_in;
    unsigned count;
    unsigned waiting;
    unsigned round;
} pthread_barrier_t;

typedef struct
{
    int unused;
} pthread_barrierattr_t;

#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

typedef volatile int pthread_spinlock_t;

typedef volatile int pthread_once_t;
#define PTHREAD_ONCE_INIT 0

typedef unsigned pthread_key_t;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *),
                   void *arg);
int pthread_join(pthread_t thread, void **result);
int pthread_detach(pthread_t thread);
void pthread_exit(void *result) __attribute__((noreturn));
pthread_t pthread_self(void);
int pthread_equal(pthread_t a, pthread_t b);
int pthread_kill(pthread_t thread, int sig);
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old);

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int state);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *size);
int pthread_attr_setguardsize(pthread_attr_t *attr, size_t size);
int pthread_attr_getguardsize(const pthread_attr_t *attr, size_t *size);

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);
int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int shared);

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_destroy(pthread_condattr_t *attr);
int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock);
int pthread_condattr_getclock(const pthread_condattr_t *attr, clockid_t *clock);
int pthread_condattr_setpshared(pthread_condattr_t *attr, int shared);

int pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr);
int pthread_rwlock_destroy(pthread_rwlock_t *lock);
int pthread_rwlock_rdlock(pthread_rwlock_t *lock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock);
int pthread_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *abstime);
int pthread_rwlock_wrlock(pthread_rwlock_t *lock);
int pthread_rwlock_trywrlock(pthread_rwlock_t *lock);
int pthread_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *abstime);
int pthread_rwlock_unlock(pthread_rwlock_t *lock);
int pthread_rwlockattr_init(pthread_rwlockattr_t *attr);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr);

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr,
                         unsigned count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);
int pthread_barrierattr_init(pthread_barrierattr_t *attr);
int pthread_barrierattr_destroy(pthread_barrierattr_t *attr);

int pthread_spin_init(pthread_spinlock_t *lock, int shared);
int pthread_spin_destroy(pthread_spinlock_t *lock);
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_spin_trylock(pthread_spinlock_t *lock);
int pthread_spin_unlock(pthread_spinlock_t *lock);

int pthread_once(pthread_once_t *once, void (*init)(void));

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);

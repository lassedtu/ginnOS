/**
 * @file pthread.c
 * @brief start, wait for and end threads (pthread.h).
 *
 * a thread gets one mapping: a guard page, its stack, and its TLS and
 * control block (struct thread) at the top. the kernel sets the control
 * block's tid to 0 when the thread has ended (CLONE_CHILD_CLEARTID) and
 * wakes a thread that waits on it: then the mapping can go. a joinable
 * thread's mapping goes in pthread_join; a detached thread's goes the
 * next time a thread starts or is detached, as no thread can unmap the
 * stack it runs on.
 */

#include "thread.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define CLONE_VM             0x00000100
#define CLONE_FS             0x00000200
#define CLONE_FILES          0x00000400
#define CLONE_SIGHAND        0x00000800
#define CLONE_THREAD         0x00010000
#define CLONE_SYSVSEM        0x00040000
#define CLONE_SETTLS         0x00080000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000

#define DEFAULT_STACK_SIZE (2 * 1024 * 1024)
#define PAGE_SIZE 4096

int __clone(int (*fn)(void *), void *stack, int flags, void *arg, int *parent_tid, void *tls,
            int *child_tid);

volatile int __libc_threaded;

// the threads of the process that have not ended; exit when it is 0
static volatile int live_threads = 1;

// detached threads that have ended or are about to: unmapped when their
// tid is 0
static struct thread *dead_threads;
static libc_lock_t dead_lock;

static size_t round_to_page(size_t size)
{
    return (size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
}

/** wait until the kernel has ended the thread */
static void wait_for_end(struct thread *t)
{
    int tid;
    while ((tid = t->tid) != 0)
    {
        __futex_wait(&t->tid, tid, NULL);
    }
}

/** unmap the detached threads that have ended */
static void free_dead_threads(void)
{
    __libc_lock(&dead_lock);
    struct thread **link = &dead_threads;
    while (*link)
    {
        struct thread *t = *link;
        if (t->tid != 0)
        {
            link = &t->next_dead;
            continue;
        }
        *link = t->next_dead;
        munmap(t->map, t->map_size);
    }
    __libc_unlock(&dead_lock);
}

static int start_thread(void *arg)
{
    struct thread *self = arg;
    pthread_exit(self->start(self->arg));
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *),
                   void *arg)
{
    pthread_attr_t defaults;
    if (!attr)
    {
        pthread_attr_init(&defaults);
        attr = &defaults;
    }
    free_dead_threads();

    size_t guard = round_to_page(attr->guard_size);
    size_t stack = round_to_page(attr->stack_size);
    size_t size = guard + stack + round_to_page(__tls_area_size());
    char *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED)
    {
        return EAGAIN;
    }
    if (guard)
    {
        mprotect(map, guard, PROT_NONE);
    }

    struct thread *t = __tls_setup(map + guard + stack);
    t->start = start;
    t->arg = arg;
    t->map = map;
    t->map_size = size;
    t->detach_state = attr->detach_state == PTHREAD_CREATE_DETACHED ? THREAD_DETACHED
                                                                    : THREAD_JOINABLE;

    __libc_threaded = 1;
    __atomic_add_fetch(&live_threads, 1, __ATOMIC_SEQ_CST);
    int flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM |
                CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;
    int tid = __clone(start_thread, map + guard + stack, flags, t, (int *)&t->tid, t,
                      (int *)&t->tid);
    if (tid < 0)
    {
        __atomic_sub_fetch(&live_threads, 1, __ATOMIC_SEQ_CST);
        munmap(map, size);
        return EAGAIN;
    }
    *thread = (pthread_t)t;
    return 0;
}

void pthread_exit(void *result)
{
    struct thread *self = thread_self();
    self->result = result;
    __pthread_key_run_destructors();

    // the last thread ends the process as exit does: atexit, stdio
    if (__atomic_sub_fetch(&live_threads, 1, __ATOMIC_SEQ_CST) == 0)
    {
        exit(0);
    }

    int state = __atomic_exchange_n(&self->detach_state, THREAD_EXITING, __ATOMIC_SEQ_CST);
    if (state == THREAD_DETACHED && self->map)
    {
        __libc_lock(&dead_lock);
        self->next_dead = dead_threads;
        dead_threads = self;
        __libc_unlock(&dead_lock);
    }

    // signals are blocked so that no handler runs on the stack of a
    // thread that another thread may unmap now
    sigset_t all;
    sigfillset(&all);
    sigprocmask(SIG_BLOCK, &all, NULL);
    for (;;)
    {
        _syscall(SYS_EXIT, 0, 0, 0, 0, 0);
    }
}

int pthread_join(pthread_t thread, void **result)
{
    struct thread *t = (struct thread *)thread;
    if (t == thread_self())
    {
        return EDEADLK;
    }
    if (t->detach_state == THREAD_DETACHED)
    {
        return EINVAL;
    }
    wait_for_end(t);
    if (result)
    {
        *result = t->result;
    }
    if (t->map)
    {
        munmap(t->map, t->map_size);
    }
    return 0;
}

int pthread_detach(pthread_t thread)
{
    struct thread *t = (struct thread *)thread;
    int expected = THREAD_JOINABLE;
    if (__atomic_compare_exchange_n(&t->detach_state, &expected, THREAD_DETACHED, false,
                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
    {
        free_dead_threads();
        return 0;
    }
    if (expected == THREAD_DETACHED)
    {
        return EINVAL;
    }
    // it is ending already: free it as pthread_join does
    return pthread_join(thread, NULL);
}

pthread_t pthread_self(void)
{
    return (pthread_t)thread_self();
}

int pthread_equal(pthread_t a, pthread_t b)
{
    return a == b;
}

int pthread_kill(pthread_t thread, int sig)
{
    int tid = ((struct thread *)thread)->tid;
    if (tid == 0)
    {
        return ESRCH;
    }
    long ret = _syscall(SYS_TGKILL, getpid(), tid, sig, 0, 0);
    return ret < 0 ? (int)-ret : 0;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *old)
{
    return sigprocmask(how, set, old) < 0 ? errno : 0;
}

void __thread_after_fork(void)
{
    struct thread *self = thread_self();
    self->tid = (int)_syscall(SYS_GETTID, 0, 0, 0, 0, 0);
    live_threads = 1;
    // the other threads are not in the child; their mappings are copies
    // that this process keeps
    dead_threads = NULL;
    dead_lock.state = 0;
}

int pthread_attr_init(pthread_attr_t *attr)
{
    attr->detach_state = PTHREAD_CREATE_JOINABLE;
    attr->stack_size = DEFAULT_STACK_SIZE;
    attr->guard_size = PAGE_SIZE;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state)
{
    if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED)
    {
        return EINVAL;
    }
    attr->detach_state = state;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state)
{
    *state = attr->detach_state;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size)
{
    if (size < PTHREAD_STACK_MIN)
    {
        return EINVAL;
    }
    attr->stack_size = size;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *size)
{
    *size = attr->stack_size;
    return 0;
}

int pthread_attr_setguardsize(pthread_attr_t *attr, size_t size)
{
    attr->guard_size = size;
    return 0;
}

int pthread_attr_getguardsize(const pthread_attr_t *attr, size_t *size)
{
    *size = attr->guard_size;
    return 0;
}

int __time_left(clockid_t clock, const struct timespec *abstime, struct timespec *left)
{
    if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
    {
        return EINVAL;
    }
    struct timespec now;
    clock_gettime(clock, &now);
    left->tv_sec = abstime->tv_sec - now.tv_sec;
    left->tv_nsec = abstime->tv_nsec - now.tv_nsec;
    if (left->tv_nsec < 0)
    {
        left->tv_nsec += 1000000000L;
        left->tv_sec--;
    }
    if (left->tv_sec < 0 || (left->tv_sec == 0 && left->tv_nsec == 0))
    {
        return ETIMEDOUT;
    }
    return 0;
}

void __libc_lock(libc_lock_t *lock)
{
    int c = 0;
    if (__atomic_compare_exchange_n(&lock->state, &c, 1, false, __ATOMIC_ACQUIRE,
                                    __ATOMIC_RELAXED))
    {
        return;
    }
    if (c != 2)
    {
        c = __atomic_exchange_n(&lock->state, 2, __ATOMIC_ACQUIRE);
    }
    while (c != 0)
    {
        __futex_wait(&lock->state, 2, NULL);
        c = __atomic_exchange_n(&lock->state, 2, __ATOMIC_ACQUIRE);
    }
}

void __libc_unlock(libc_lock_t *lock)
{
    if (__atomic_exchange_n(&lock->state, 0, __ATOMIC_RELEASE) == 2)
    {
        __futex_wake(&lock->state, 1);
    }
}

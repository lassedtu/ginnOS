/**
 * @file thread.h
 * @brief libc's own view of a thread: its control block, which the thread
 *        pointer points to (%fs:0 on x86_64, %gs:0 on i686), and the
 *        futex calls the locks are made of.
 */

#ifndef LIBC_THREAD_H
#define LIBC_THREAD_H

#include <stddef.h>
#include <stdint.h>
#include <syscall.h>
#include <time.h>

/**
 * the thread control block. the thread's TLS block lies just below it
 * (x86 "variant II": GCC's code reaches a TLS variable at a negative
 * offset from the thread pointer). the first word points to the block
 * itself, as GCC's code reads the thread pointer from %fs:0 (%gs:0).
 * canary is where GCC's stack protector looks (%fs:0x28 and %gs:0x14:
 * the sixth word on both).
 */
struct thread
{
    struct thread *self;
    void *dtv;         // unused: there are no dynamic modules
    void *unused[3];
    uintptr_t canary;
    void *area;        // the memory of the TLS block and this block
    size_t area_size;

    // pthreads (pthread.c)
    volatile int tid;          // the kernel's id; 0 when the thread has ended
    volatile int detach_state; // THREAD_JOINABLE, THREAD_DETACHED, THREAD_EXITING
    void *(*start)(void *);
    void *arg;
    void *result;
    void *map;       // the stack and TLS of a pthread_create thread, else NULL
    size_t map_size;
    struct thread *next_dead; // the list of detached threads to free
};

#define THREAD_JOINABLE 0
#define THREAD_DETACHED 1
#define THREAD_EXITING  2

/** the calling thread's control block */
static inline struct thread *thread_self(void)
{
    struct thread *self;
#ifdef __x86_64__
    __asm__("mov %%fs:0, %0" : "=r"(self));
#else
    __asm__("mov %%gs:0, %0" : "=r"(self));
#endif
    return self;
}

/**
 * the bytes one thread needs for its TLS block and control block,
 * alignment included.
 */
size_t __tls_area_size(void);

/**
 * set up a thread's TLS block (a copy of the program's template, the rest
 * zeroed) and control block in area.
 * @param area __tls_area_size() bytes.
 * @return the control block: the new thread's thread pointer.
 */
struct thread *__tls_setup(void *area);

/**
 * give the first thread its TLS: called by crt0 before main.
 */
void __init_tls(void);

/** 1 after the first pthread_create: stdio and malloc lock from then on */
extern volatile int __libc_threaded;

/** the first thread of a forked child is the only one (fork) */
void __thread_after_fork(void);

/** run the destructors of the calling thread's keys (pthread_exit) */
void __pthread_key_run_destructors(void);

#define FUTEX_WAIT_PRIVATE 128
#define FUTEX_WAKE_PRIVATE 129

/**
 * sleep while *word == value, for at most *left (NULL: no limit).
 * @return 0, or a negative errno (-EAGAIN, -ETIMEDOUT, -EINTR).
 */
static inline int __futex_wait(volatile int *word, int value, const struct timespec *left)
{
    return (int)_syscall(SYS_FUTEX, (long)word, FUTEX_WAIT_PRIVATE, value, (long)left, 0);
}

/** wake at most count threads that sleep on word */
static inline void __futex_wake(volatile int *word, int count)
{
    _syscall(SYS_FUTEX, (long)word, FUTEX_WAKE_PRIVATE, count, 0, 0);
}

/**
 * the time left until an absolute time on a clock.
 * @return 0 with *left set, ETIMEDOUT if the time has come, or EINVAL.
 */
int __time_left(clockid_t clock, const struct timespec *abstime, struct timespec *left);

/** a lock for libc's own data; all zero is free */
typedef struct
{
    volatile int state; // 0 free, 1 locked, 2 locked with threads asleep
} libc_lock_t;

void __libc_lock(libc_lock_t *lock);
void __libc_unlock(libc_lock_t *lock);

#endif

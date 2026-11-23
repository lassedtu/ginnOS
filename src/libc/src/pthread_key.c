/**
 * @file pthread_key.c
 * @brief one-time initialization and thread-specific data (pthread.h).
 *
 * a key is a slot number. each thread keeps its values in thread-local
 * storage, each with the generation of the key it was set for, so a key
 * made again in a slot that was deleted starts as NULL in every thread.
 */

#include "thread.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>

#define ONCE_NEW     0
#define ONCE_RUNNING 1
#define ONCE_DONE    2

int pthread_once(pthread_once_t *once, void (*init)(void))
{
    int state = ONCE_NEW;
    if (__atomic_compare_exchange_n(once, &state, ONCE_RUNNING, false, __ATOMIC_ACQUIRE,
                                    __ATOMIC_ACQUIRE))
    {
        init();
        __atomic_store_n(once, ONCE_DONE, __ATOMIC_RELEASE);
        __futex_wake(once, 0x7FFFFFFF);
        return 0;
    }
    while ((state = __atomic_load_n(once, __ATOMIC_ACQUIRE)) == ONCE_RUNNING)
    {
        __futex_wait(once, ONCE_RUNNING, NULL);
    }
    return 0;
}

static struct
{
    bool used;
    unsigned generation;
    void (*destructor)(void *);
} keys[PTHREAD_KEYS_MAX];

static libc_lock_t keys_lock;

static __thread struct
{
    const void *value;
    unsigned generation;
} values[PTHREAD_KEYS_MAX];

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    __libc_lock(&keys_lock);
    for (unsigned i = 0; i < PTHREAD_KEYS_MAX; i++)
    {
        if (!keys[i].used)
        {
            keys[i].used = true;
            keys[i].generation++;
            keys[i].destructor = destructor;
            __libc_unlock(&keys_lock);
            *key = i;
            return 0;
        }
    }
    __libc_unlock(&keys_lock);
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key)
{
    if (key >= PTHREAD_KEYS_MAX || !keys[key].used)
    {
        return EINVAL;
    }
    __libc_lock(&keys_lock);
    keys[key].used = false;
    keys[key].destructor = NULL;
    __libc_unlock(&keys_lock);
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    if (key >= PTHREAD_KEYS_MAX || !keys[key].used ||
        values[key].generation != keys[key].generation)
    {
        return NULL;
    }
    return (void *)values[key].value;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    if (key >= PTHREAD_KEYS_MAX || !keys[key].used)
    {
        return EINVAL;
    }
    values[key].value = value;
    values[key].generation = keys[key].generation;
    return 0;
}

void __pthread_key_run_destructors(void)
{
    // a destructor may set values again: try a few rounds, as POSIX says
    for (int round = 0; round < PTHREAD_DESTRUCTOR_ITERATIONS; round++)
    {
        bool ran = false;
        for (unsigned i = 0; i < PTHREAD_KEYS_MAX; i++)
        {
            void *value = pthread_getspecific(i);
            void (*destructor)(void *) = keys[i].destructor;
            if (value && destructor)
            {
                values[i].value = NULL;
                destructor(value);
                ran = true;
            }
        }
        if (!ran)
        {
            return;
        }
    }
}

#pragma once

/**
 * @file semaphore.h
 * @brief unnamed POSIX semaphores, for the threads of one process.
 *        sem_init with pshared set fails with ENOSYS.
 */

#include <time.h>

typedef struct
{
    volatile int value;
    volatile int waiters;
} sem_t;

#define SEM_VALUE_MAX 0x7FFFFFFF

int sem_init(sem_t *sem, int pshared, unsigned value);
int sem_destroy(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_timedwait(sem_t *sem, const struct timespec *abstime);
int sem_post(sem_t *sem);
int sem_getvalue(sem_t *sem, int *value);

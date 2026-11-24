#pragma once

/**
 * @file flock.h
 * @brief advisory whole-file locks (flock).
 *
 * an open-file description holds a lock on its file: shared (many may
 * hold one) or exclusive (one only). the locks are advisory: they stop
 * other flock calls, not reads or writes. a description's lock goes when
 * it is unlocked, or when the description's last descriptor closes.
 */

#include "common/stdint.h"
#include "kernel/vfs/file.h"

// flock operations (ABI, as in the libc's sys/file.h)
#define LOCK_SH 1 // a shared lock
#define LOCK_EX 2 // an exclusive lock
#define LOCK_NB 4 // with LOCK_SH or LOCK_EX: fail at once, do not wait
#define LOCK_UN 8 // release the lock

/**
 * take, change or release a description's lock.
 * @param file a filesystem file.
 * @param operation LOCK_SH or LOCK_EX (with LOCK_NB), or LOCK_UN.
 * @return 0, or -EWOULDBLOCK (LOCK_NB and another holds a conflicting
 *         lock), -ERESTARTSYS (a signal while waiting), -ENOLCK (the lock
 *         table is full), -EINVAL.
 */
int32_t flock_apply(file_t *file, uint32_t operation);

/**
 * release the lock a description holds (its last descriptor closed).
 */
void flock_release(file_t *file);

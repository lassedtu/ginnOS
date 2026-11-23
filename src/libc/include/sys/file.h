#pragma once

/**
 * @file sys/file.h
 * @brief flock: advisory locks on whole files.
 *
 * a lock belongs to the open file (dup and fork share it) and goes when
 * the file is unlocked or its last descriptor closes. the locks only stop
 * other flock calls, not reads or writes.
 */

#define LOCK_SH 1 // a shared lock: others may hold shared locks too
#define LOCK_EX 2 // an exclusive lock: no one else holds one
#define LOCK_NB 4 // with LOCK_SH or LOCK_EX: fail with EWOULDBLOCK, do not wait
#define LOCK_UN 8 // release the lock

/**
 * take, change or release the lock of the file open as fd.
 * @return 0, or -1 (errno: EWOULDBLOCK, EINTR, EBADF, EINVAL, ENOLCK).
 */
int flock(int fd, int operation);

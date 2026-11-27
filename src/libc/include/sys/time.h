#pragma once

/**
 * @file sys/time.h
 * @brief struct timeval and gettimeofday. it also gives select() and
 *        fd_set (sys/select.h), as on other Unix systems.
 */

#include <sys/types.h>

#ifndef GINNOS_TIMEVAL
#define GINNOS_TIMEVAL
struct timeval
{
    long tv_sec;
    suseconds_t tv_usec;
};
#endif

#include <sys/select.h>

/**
 * the wall-clock time in seconds and microseconds since 1970.
 * @param tv receives it.
 * @param tz must be NULL (there are no time zones).
 * @return 0, or -1 (errno set).
 */
int gettimeofday(struct timeval *tv, void *tz);

/**
 * set a file's access and modification times (times[0], times[1]) in
 * microseconds; with times NULL, both to the time now.
 * @return 0, or -1 (errno set).
 */
int utimes(const char *path, const struct timeval times[2]);

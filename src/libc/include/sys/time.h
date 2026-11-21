#pragma once

/**
 * @file sys/time.h
 * @brief struct timeval and gettimeofday.
 */

#ifndef GINNOS_TIMEVAL
#define GINNOS_TIMEVAL
struct timeval
{
    long tv_sec;
    long tv_usec;
};
#endif

/**
 * the wall-clock time in seconds and microseconds since 1970.
 * @param tv receives it.
 * @param tz must be NULL (there are no time zones).
 * @return 0, or -1 (errno set).
 */
int gettimeofday(struct timeval *tv, void *tz);

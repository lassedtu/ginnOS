#pragma once

/**
 * the system clocks.
 *
 *   CLOCK_REALTIME   seconds since 1970 (from the RTC at boot, then the
 *                    timer ticks)
 *   CLOCK_MONOTONIC  time since boot; never set, never goes back
 *   CLOCK_PROCESS_CPUTIME_ID  user and system time of the calling process
 *
 * the resolution is one timer tick (10 ms at 100 Hz).
 */

#include "common/stdint.h"

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2

#define NSEC_PER_SEC 1000000000u

/**
 * struct timespec (ABI: long seconds and nanoseconds, as the libc's
 * <time.h> has it).
 */
struct ktimespec
{
    long tv_sec;
    long tv_nsec;
};

/**
 * read the wall-clock time from the RTC. call once at boot.
 */
void clock_init(void);

/**
 * set up the queue nanosleep sleeps on. call once at boot, before the
 * first nanosleep.
 */
void clock_init_sleep(void);

/**
 * the wall-clock time in seconds since 1970 (0 if the RTC gave none).
 */
uint32_t clock_realtime_seconds(void);

/**
 * a timespec from a number of timer ticks.
 */
void clock_ticks_to_timespec(uint64_t ticks, struct ktimespec *out);

/**
 * the wall-clock time as a timespec.
 */
void clock_realtime(struct ktimespec *out);

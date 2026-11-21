#pragma once

#include <sys/types.h>

/**
 * @file sys/times.h
 * @brief process CPU times.
 *
 * the four times are in clock ticks: sysconf-free ginnOS has 100 per
 * second (CLK_TCK). struct tms is ABI (kernel/time/clock.c).
 */

#define CLK_TCK 100

struct tms
{
    clock_t tms_utime;  // user time of the process
    clock_t tms_stime;  // system time of the process
    clock_t tms_cutime; // user time of its reaped children
    clock_t tms_cstime; // system time of its reaped children
};

/**
 * the CPU times of the process and its reaped children.
 * @return the ticks since boot.
 */
clock_t times(struct tms *buf);

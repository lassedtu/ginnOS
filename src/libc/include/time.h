#pragma once

/**
 * @file time.h
 * @brief clocks, sleeping and calendar time (UTC only: there are no time
 * zones, so local time is UTC).
 *
 * struct timespec and the clock ids are ABI (src/kernel/time/clock.h).
 */

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef long time_t;
typedef long clock_t;
typedef int clockid_t;
typedef unsigned int size_t;

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2

// clock() counts in microseconds, as POSIX asks
#define CLOCKS_PER_SEC 1000000l

struct timespec
{
    time_t tv_sec;
    long tv_nsec;
};

struct tm
{
    int tm_sec;   // 0-60
    int tm_min;   // 0-59
    int tm_hour;  // 0-23
    int tm_mday;  // 1-31
    int tm_mon;   // 0-11
    int tm_year;  // years since 1900
    int tm_wday;  // 0-6, Sunday = 0
    int tm_yday;  // 0-365
    int tm_isdst; // always 0
};

time_t time(time_t *out);
int clock_gettime(clockid_t id, struct timespec *out);
int nanosleep(const struct timespec *span, struct timespec *left);
clock_t clock(void);
double difftime(time_t end, time_t start);

struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *gmtime(const time_t *t);
struct tm *localtime_r(const time_t *t, struct tm *out);
struct tm *localtime(const time_t *t);
time_t timegm(struct tm *tm);
time_t mktime(struct tm *tm);

char *asctime_r(const struct tm *tm, char *buf);
char *asctime(const struct tm *tm);
char *ctime(const time_t *t);

/**
 * format a time. supports %a %A %b %B %c %d %e %H %I %j %m %M %n %p %S %t
 * %u %w %y %Y %Z %z %F %T %D %R and %%.
 * @return the length written, or 0 if it does not fit.
 */
size_t strftime(char *buf, size_t size, const char *format, const struct tm *tm);

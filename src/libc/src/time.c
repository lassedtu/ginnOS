#include "time.h"

#include "errno.h"
#include "stdio.h"
#include "string.h"
#include "syscall.h"
#include "sys/time.h"
#include "sys/times.h"
#include "unistd.h"

static int check(int ret)
{
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

int clock_gettime(clockid_t id, struct timespec *out)
{
    return check(_syscall(SYS_CLOCK_GETTIME, id, (long)out, 0, 0, 0));
}

int nanosleep(const struct timespec *span, struct timespec *left)
{
    return check(_syscall(SYS_NANOSLEEP, (long)span, (long)left, 0, 0, 0));
}

clock_t times(struct tms *buf)
{
    return (clock_t)check(_syscall(SYS_TIMES, (long)buf, 0, 0, 0, 0));
}

time_t time(time_t *out)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    if (out)
        *out = now.tv_sec;
    return now.tv_sec;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) < 0)
        return -1;
    tv->tv_sec = now.tv_sec;
    tv->tv_usec = now.tv_nsec / 1000;
    return 0;
}

clock_t clock(void)
{
    struct timespec used;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &used) < 0)
        return -1;
    return used.tv_sec * CLOCKS_PER_SEC + used.tv_nsec / 1000;
}

double difftime(time_t end, time_t start)
{
    return (double)(end - start);
}

unsigned int sleep(unsigned int seconds)
{
    struct timespec span = {(time_t)seconds, 0};
    struct timespec left = {0, 0};
    if (nanosleep(&span, &left) < 0)
        return (unsigned int)left.tv_sec + (left.tv_nsec > 0);
    return 0;
}

int usleep(unsigned int microseconds)
{
    struct timespec span = {(time_t)(microseconds / 1000000u),
                            (long)(microseconds % 1000000u) * 1000};
    return nanosleep(&span, NULL);
}

/*
 * calendar time, all UTC
 */

static int is_leap(long year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static const int month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    long days = *t / 86400;
    long seconds = *t % 86400;
    if (seconds < 0)
    {
        seconds += 86400;
        days--;
    }

    out->tm_hour = (int)(seconds / 3600);
    out->tm_min = (int)(seconds % 3600 / 60);
    out->tm_sec = (int)(seconds % 60);
    out->tm_wday = (int)((days % 7 + 11) % 7); // 1970-01-01 was a Thursday
    out->tm_isdst = 0;
    out->tm_gmtoff = 0;
    out->tm_zone = "UTC";

    long year = 1970;
    for (;;)
    {
        long length = is_leap(year) ? 366 : 365;
        if (days >= 0 && days < length)
            break;
        if (days < 0)
        {
            year--;
            days += is_leap(year) ? 366 : 365;
        }
        else
        {
            days -= length;
            year++;
        }
    }
    out->tm_year = (int)(year - 1900);
    out->tm_yday = (int)days;

    int month = 0;
    for (; month < 12; month++)
    {
        int length = month_days[month] + (month == 1 && is_leap(year));
        if (days < length)
            break;
        days -= length;
    }
    out->tm_mon = month;
    out->tm_mday = (int)days + 1;
    return out;
}

struct tm *gmtime(const time_t *t)
{
    static struct tm result;
    return gmtime_r(t, &result);
}

struct tm *localtime_r(const time_t *t, struct tm *out)
{
    return gmtime_r(t, out);
}

struct tm *localtime(const time_t *t)
{
    return gmtime(t);
}

time_t timegm(struct tm *tm)
{
    long year = tm->tm_year + 1900L;
    long days = 0;
    for (long y = 1970; y < year; y++)
        days += is_leap(y) ? 366 : 365;
    for (long y = year; y < 1970; y++)
        days -= is_leap(y) ? 366 : 365;
    for (int m = 0; m < tm->tm_mon && m < 12; m++)
        days += month_days[m] + (m == 1 && is_leap(year));
    days += tm->tm_mday - 1;

    time_t t = days * 86400 + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
    gmtime_r(&t, tm); // fills tm_wday and tm_yday, and normalises
    return t;
}

time_t mktime(struct tm *tm)
{
    return timegm(tm);
}

static const char *const day_names[7] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                         "Thursday", "Friday", "Saturday"};
static const char *const month_names[12] = {"January", "February", "March", "April",
                                            "May", "June", "July", "August",
                                            "September", "October", "November", "December"};

char *asctime_r(const struct tm *tm, char *buf)
{
    strftime(buf, 26, "%a %b %e %H:%M:%S %Y\n", tm);
    return buf;
}

char *asctime(const struct tm *tm)
{
    static char buf[26];
    return asctime_r(tm, buf);
}

char *ctime(const time_t *t)
{
    return asctime(gmtime(t));
}

/**
 * append text to the output; false if it does not fit.
 */
static int put(char *buf, size_t size, size_t *used, const char *text)
{
    size_t len = strlen(text);
    if (*used + len >= size)
        return 0;
    memcpy(buf + *used, text, len);
    *used += len;
    return 1;
}

/**
 * a number with a minimum width, padded with pad.
 */
static void number(char *out, int value, int width, char pad)
{
    char digits[12];
    int len = 0;
    unsigned int n = value < 0 ? (unsigned int)-value : (unsigned int)value;
    do
    {
        digits[len++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);

    int i = 0;
    if (value < 0)
        out[i++] = '-';
    for (int fill = len; fill < width; fill++)
        out[i++] = pad;
    while (len > 0)
        out[i++] = digits[--len];
    out[i] = '\0';
}

size_t strftime(char *buf, size_t size, const char *format, const struct tm *tm)
{
    size_t used = 0;
    char field[64];

    for (const char *f = format; *f; f++)
    {
        if (*f != '%')
        {
            field[0] = *f;
            field[1] = '\0';
        }
        else
        {
            f++;
            field[0] = '\0';
            switch (*f)
            {
            case 'a': strncpy(field, day_names[tm->tm_wday % 7], 3); field[3] = '\0'; break;
            case 'A': strcpy(field, day_names[tm->tm_wday % 7]); break;
            case 'b':
            case 'h': strncpy(field, month_names[tm->tm_mon % 12], 3); field[3] = '\0'; break;
            case 'B': strcpy(field, month_names[tm->tm_mon % 12]); break;
            case 'd': number(field, tm->tm_mday, 2, '0'); break;
            case 'e': number(field, tm->tm_mday, 2, ' '); break;
            case 'H': number(field, tm->tm_hour, 2, '0'); break;
            case 'I': number(field, (tm->tm_hour + 11) % 12 + 1, 2, '0'); break;
            case 'j': number(field, tm->tm_yday + 1, 3, '0'); break;
            case 'm': number(field, tm->tm_mon + 1, 2, '0'); break;
            case 'M': number(field, tm->tm_min, 2, '0'); break;
            case 'S': number(field, tm->tm_sec, 2, '0'); break;
            case 'p': strcpy(field, tm->tm_hour < 12 ? "AM" : "PM"); break;
            case 'u': number(field, tm->tm_wday ? tm->tm_wday : 7, 1, '0'); break;
            case 'w': number(field, tm->tm_wday, 1, '0'); break;
            case 'y': number(field, tm->tm_year % 100, 2, '0'); break;
            case 'Y': number(field, tm->tm_year + 1900, 4, '0'); break;
            case 'Z': strcpy(field, "UTC"); break;
            case 'z': strcpy(field, "+0000"); break;
            case 'n': strcpy(field, "\n"); break;
            case 't': strcpy(field, "\t"); break;
            case '%': strcpy(field, "%"); break;
            case 'F': strftime(field, sizeof(field), "%Y-%m-%d", tm); break;
            case 'T': strftime(field, sizeof(field), "%H:%M:%S", tm); break;
            case 'R': strftime(field, sizeof(field), "%H:%M", tm); break;
            case 'D': strftime(field, sizeof(field), "%m/%d/%y", tm); break;
            case 'c': strftime(field, sizeof(field), "%a %b %e %H:%M:%S %Y", tm); break;
            case '\0': f--; break;
            default: field[0] = '%'; field[1] = *f; field[2] = '\0'; break;
            }
        }
        if (!put(buf, size, &used, field))
            return 0;
    }

    buf[used] = '\0';
    return used;
}

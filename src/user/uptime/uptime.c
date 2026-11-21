/**
 * @file uptime.c
 * @brief Print how long the system has been running.
 */

#include <stdio.h>
#include <time.h>

int main(void)
{
    struct timespec since_boot;
    clock_gettime(CLOCK_MONOTONIC, &since_boot);

    int seconds = (int)since_boot.tv_sec;
    int days = seconds / 86400;
    int hours = seconds % 86400 / 3600;
    int minutes = seconds % 3600 / 60;

    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    char clock_text[16];
    strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &tm);

    printf("%s up ", clock_text);
    if (days > 0)
        printf("%d day%s, ", days, days == 1 ? "" : "s");
    if (hours > 0)
        printf("%d:%d%d\n", hours, minutes / 10, minutes % 10);
    else
        printf("%d min, %d sec\n", minutes, seconds % 60);
    return 0;
}

/**
 * @file sleep.c
 * @brief Wait for a time: "sleep 2" or "sleep 0.5" seconds.
 */

#include <stdio.h>
#include <time.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: sleep <seconds>[.fraction]\n");
        return 1;
    }

    // whole seconds, then up to 9 digits of fraction
    struct timespec span = {0, 0};
    const char *c = argv[1];
    for (; *c >= '0' && *c <= '9'; c++)
        span.tv_sec = span.tv_sec * 10 + (*c - '0');
    if (*c == '.')
    {
        long scale = 100000000;
        for (c++; *c >= '0' && *c <= '9' && scale > 0; c++, scale /= 10)
            span.tv_nsec += (*c - '0') * scale;
    }

    // a signal ends the sleep and, by default, the program
    nanosleep(&span, NULL);
    return 0;
}

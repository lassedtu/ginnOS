#include "../include/stdlib.h"
#include "../include/unistd.h"

/**
 * @file stdlib.c
 * @brief This file contains the implementations of general utility functions.
 */

int atoi(const char *s)
{
    int result = 0;
    int sign = 1;

    /* skip whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;

    /* handle sign */
    if (*s == '-')
    {
        sign = -1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }

    /* convert digits */
    while (*s >= '0' && *s <= '9')
    {
        result = result * 10 + (*s - '0');
        s++;
    }

    return result * sign;
}

void exit(int status)
{
    _exit(status);
}

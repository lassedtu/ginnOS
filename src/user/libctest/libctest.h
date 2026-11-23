#pragma once

/**
 * @file libctest.h
 * @brief libctest: the check counter its files share.
 */

#include <stdio.h>

extern int checks;
extern int failures;

#define CHECK(condition)                                                                   \
    do                                                                                     \
    {                                                                                      \
        checks++;                                                                          \
        if (!(condition))                                                                  \
        {                                                                                  \
            failures++;                                                                    \
            printf("libctest: %s:%d: %s\n", __FILE__, __LINE__, #condition);               \
        }                                                                                  \
    } while (0)

// wide.c
void test_wide(void);

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

/** as CHECK, but counted once for a whole loop (a failure still prints) */
#define CHECK_QUIET(condition)                                                             \
    do                                                                                     \
    {                                                                                      \
        if (!(condition))                                                                  \
        {                                                                                  \
            failures++;                                                                    \
            printf("libctest: %s:%d: %s\n", __FILE__, __LINE__, #condition);               \
        }                                                                                  \
    } while (0)

// wide.c
void test_wide(void);
// regex.c
void test_regex(void);
// posix.c
void test_posix(void);
// files.c
void test_files(void);
// threads.c
void test_threads(void);
// net.c
void test_net(void);
// random.c
void test_random(void);

#pragma once

/**
 * @file hosttest.h
 * @brief a small harness for the library tests that run on the host.
 *
 * CHECK records a failure and goes on; hosttest_done prints the result
 * and gives the exit status. golden images are PPM files in GOLDEN_DIR:
 * hosttest_golden compares an image with its golden file, or writes the
 * file when GOLDEN=update. the image is always written to the current
 * directory too, to look at.
 */

#include <stdbool.h>
#include <stdint.h>

#define CHECK(cond) hosttest_check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b)                                                                     \
    hosttest_check_eq((long long)(a), (long long)(b), #a " == " #b, __FILE__, __LINE__)

void hosttest_check(bool ok, const char *what, const char *file, int line);
void hosttest_check_eq(long long a, long long b, const char *what, const char *file, int line);

/**
 * compare an image with its golden file (or write the golden file).
 * @param name the file name without ".ppm".
 * @param rgb the pixels, 3 bytes each (red, green, blue), row by row.
 */
void hosttest_golden(const char *name, const uint8_t *rgb, int width, int height);

/**
 * compare an image with a reference image that is allowed to differ a
 * little (the idunn reference cells, which Chrome draws). prints the
 * largest and the mean difference of a colour channel, and fails when
 * either is over its limit. writes <name>.ppm, <name>-ref.ppm and
 * <name>-diff.ppm (the difference, 8 times brighter) to look at.
 * @param ref the reference pixels, the same size and format as rgb.
 * @param limit the largest difference allowed (edge pixels can differ much).
 * @param mean_limit the mean difference allowed (a wrong place or colour
 *        moves the mean).
 */
void hosttest_compare(const char *name, const uint8_t *rgb, const uint8_t *ref, int width,
                      int height, int limit, double mean_limit);

/**
 * print the result.
 * @return the exit status: 0 if every check passed.
 */
int hosttest_done(const char *suite);

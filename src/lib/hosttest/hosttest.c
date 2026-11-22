/**
 * @file hosttest.c
 * @brief the host test harness (see hosttest.h).
 */

#include "hosttest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;

void hosttest_check(bool ok, const char *what, const char *file, int line)
{
    checks++;
    if (!ok)
    {
        failures++;
        printf("  FAIL %s:%d: %s\n", file, line, what);
    }
}

void hosttest_check_eq(long long a, long long b, const char *what, const char *file, int line)
{
    checks++;
    if (a != b)
    {
        failures++;
        printf("  FAIL %s:%d: %s (%lld, %lld)\n", file, line, what, a, b);
    }
}

static bool write_ppm(const char *path, const uint8_t *rgb, int width, int height)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    size_t size = (size_t)width * (size_t)height * 3;
    bool ok = fwrite(rgb, 1, size, f) == size;
    fclose(f);
    return ok;
}

/**
 * read a P6 PPM with maxval 255.
 * @return the pixels (malloc), or NULL.
 */
static uint8_t *read_ppm(const char *path, int *width, int *height)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    int maxval;
    uint8_t *rgb = NULL;
    if (fscanf(f, "P6 %d %d %d", width, height, &maxval) == 3 && maxval == 255 &&
        fgetc(f) != EOF)
    {
        size_t size = (size_t)*width * (size_t)*height * 3;
        rgb = malloc(size);
        if (rgb && fread(rgb, 1, size, f) != size)
        {
            free(rgb);
            rgb = NULL;
        }
    }
    fclose(f);
    return rgb;
}

void hosttest_golden(const char *name, const uint8_t *rgb, int width, int height)
{
    char path[512];
    snprintf(path, sizeof(path), "%s.ppm", name);
    write_ppm(path, rgb, width, height);

    const char *dir = getenv("GOLDEN_DIR");
    const char *mode = getenv("GOLDEN");
    snprintf(path, sizeof(path), "%s/%s.ppm", dir ? dir : "golden", name);
    checks++;
    if (mode && strcmp(mode, "update") == 0)
    {
        if (!write_ppm(path, rgb, width, height))
        {
            failures++;
            printf("  FAIL golden %s: cannot write %s\n", name, path);
        }
        return;
    }

    int golden_width;
    int golden_height;
    uint8_t *golden = read_ppm(path, &golden_width, &golden_height);
    if (!golden)
    {
        failures++;
        printf("  FAIL golden %s: no golden image (run with GOLDEN=update)\n", name);
        return;
    }
    if (golden_width != width || golden_height != height)
    {
        failures++;
        printf("  FAIL golden %s: %dx%d, the golden image is %dx%d\n", name, width, height,
               golden_width, golden_height);
    }
    else
    {
        int differ = 0;
        int first = -1;
        for (int i = 0; i < width * height; i++)
        {
            if (memcmp(rgb + i * 3, golden + i * 3, 3) != 0)
            {
                if (first < 0)
                    first = i;
                differ++;
            }
        }
        if (differ > 0)
        {
            failures++;
            printf("  FAIL golden %s: %d pixels differ, the first at (%d, %d)\n", name, differ,
                   first % width, first / width);
        }
    }
    free(golden);
}

void hosttest_compare(const char *name, const uint8_t *rgb, const uint8_t *ref, int width,
                      int height, int limit)
{
    size_t size = (size_t)width * (size_t)height * 3;
    uint8_t *diff = malloc(size);
    int largest = 0;
    long total = 0;
    for (size_t i = 0; i < size; i++)
    {
        int d = rgb[i] > ref[i] ? rgb[i] - ref[i] : ref[i] - rgb[i];
        if (d > largest)
            largest = d;
        total += d;
        if (diff)
            diff[i] = (uint8_t)(d * 8 > 255 ? 255 : d * 8);
    }
    char path[512];
    snprintf(path, sizeof(path), "%s.ppm", name);
    write_ppm(path, rgb, width, height);
    snprintf(path, sizeof(path), "%s-ref.ppm", name);
    write_ppm(path, ref, width, height);
    if (diff)
    {
        snprintf(path, sizeof(path), "%s-diff.ppm", name);
        write_ppm(path, diff, width, height);
        free(diff);
    }

    double mean = size ? (double)total / (double)size : 0;
    printf("  compare %-10s largest %3d (limit %3d), mean %.2f\n", name, largest, limit, mean);
    checks++;
    if (largest > limit)
    {
        failures++;
        printf("  FAIL compare %s: differs by %d, more than %d\n", name, largest, limit);
    }
}

int hosttest_done(const char *suite)
{
    printf("%s: %d checks, %d failed\n", suite, checks, failures);
    return failures == 0 ? 0 : 1;
}

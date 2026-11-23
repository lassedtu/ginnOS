/**
 * @file posix.c
 * @brief libctest: the POSIX parts the Unix tools need: getline, the
 *        parts of a path, device numbers, and the time zone fields.
 */

#include "libctest.h"

#include <inttypes.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

static void test_getline(void)
{
    char path[] = "/tmp/libctest.XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0)
        return;
    const char text[] = "one\nlonger line two, longer than the first buffer of all\nthree";
    CHECK(write(fd, text, sizeof(text) - 1) == (ssize_t)(sizeof(text) - 1));
    close(fd);

    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    char *line = NULL;
    size_t size = 0;
    CHECK(getline(&line, &size, f) == 4 && strcmp(line, "one\n") == 0);
    ssize_t second = (ssize_t)(strchr(text + 4, '\n') - (text + 4)) + 1;
    CHECK(getline(&line, &size, f) == second && line[second - 1] == '\n' && size > 64);
    CHECK(getline(&line, &size, f) == 5 && strcmp(line, "three") == 0); // no newline at the end
    CHECK(getline(&line, &size, f) == -1);
    rewind(f);
    CHECK(getdelim(&line, &size, 'e', f) == 3 && strcmp(line, "one") == 0);
    free(line);
    fclose(f);
    unlink(path);
}

/** basename and dirname change their argument: each check has its own copy */
static int base_is(const char *path, const char *want)
{
    char copy[64];
    strcpy(copy, path);
    return strcmp(basename(copy), want) == 0;
}

static int dir_is(const char *path, const char *want)
{
    char copy[64];
    strcpy(copy, path);
    return strcmp(dirname(copy), want) == 0;
}

static void test_paths(void)
{
    CHECK(base_is("/usr/lib", "lib") && base_is("/usr/lib/", "lib") && base_is("lib", "lib"));
    CHECK(base_is("/", "/") && base_is("", "."));
    CHECK(dir_is("/usr/lib", "/usr") && dir_is("/usr/lib//", "/usr") && dir_is("usr", "."));
    CHECK(dir_is("/usr", "/") && dir_is("/", "/") && dir_is("a//b", "a"));
}

static void test_numbers(void)
{
    CHECK(strtoumax("18446744073709551615", NULL, 10) == UINTMAX_MAX);
    CHECK(strtoimax("-42", NULL, 10) == -42);
    unsigned int dev = makedev(8, 300);
    CHECK(major(dev) == 8 && minor(dev) == 300);
}

static void test_zone(void)
{
    time_t t = 0;
    struct tm tm;
    gmtime_r(&t, &tm);
    CHECK(tm.tm_gmtoff == 0 && strcmp(tm.tm_zone, "UTC") == 0);
}

void test_posix(void)
{
    test_getline();
    test_paths();
    test_numbers();
    test_zone();
}

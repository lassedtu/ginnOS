/**
 * @file files.c
 * @brief libctest: the calls that name a file relative to an open
 *        directory (openat and the other *at calls), and fdopendir.
 */

#include "libctest.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** the names in a directory stream, counted */
static int count_entries(DIR *dir, const char *name, int *found)
{
    int count = 0;
    struct dirent *d;
    while ((d = readdir(dir)))
    {
        count++;
        if (strcmp(d->d_name, name) == 0)
            *found = 1;
    }
    return count;
}

static void test_at_calls(void)
{
    char top[] = "/tmp/libctest-at.XXXXXX";
    CHECK(mkdtemp(top) != NULL);
    int dir = open(top, O_RDONLY | O_DIRECTORY);
    CHECK(dir >= 0);

    // make a folder and a file in it, through the directory fd
    CHECK(mkdirat(dir, "sub", 0755) == 0);
    int sub = openat(dir, "sub", O_RDONLY | O_DIRECTORY);
    CHECK(sub >= 0);
    int fd = openat(sub, "file", O_WRONLY | O_CREAT | O_EXCL, 0640);
    CHECK(fd >= 0);
    CHECK(write(fd, "hello", 5) == 5);
    close(fd);

    struct stat st;
    CHECK(fstatat(dir, "sub/file", &st, 0) == 0 && st.st_size == 5);
    CHECK((st.st_mode & 0777) == 0640);
    CHECK(faccessat(sub, "file", R_OK, 0) == 0);
    CHECK(faccessat(sub, "missing", F_OK, 0) == -1 && errno == ENOENT);
    CHECK(fchmodat(sub, "file", 0600, 0) == 0);
    CHECK(fstatat(sub, "file", &st, 0) == 0 && (st.st_mode & 0777) == 0600);

    // an absolute path does not use the fd
    char absolute[64];
    snprintf(absolute, sizeof(absolute), "%s/sub/file", top);
    CHECK(fstatat(12345, absolute, &st, 0) == 0);

    // rename from one directory fd to another
    CHECK(renameat(sub, "file", dir, "moved") == 0);
    CHECK(fstatat(dir, "moved", &st, 0) == 0 && st.st_size == 5);
    CHECK(fstatat(sub, "file", &st, 0) == -1 && errno == ENOENT);

    // fdopendir reads the directory the fd is open on
    int again = openat(dir, ".", O_RDONLY | O_DIRECTORY);
    DIR *stream = fdopendir(again);
    CHECK(stream != NULL);
    int found = 0;
    if (stream)
    {
        CHECK(count_entries(stream, "moved", &found) >= 2);
        closedir(stream);
    }
    CHECK(found);

    // errors: a bad fd, a file as the directory, an empty path, a folder
    // removed without AT_REMOVEDIR
    CHECK(fstatat(999, "moved", &st, 0) == -1 && errno == EBADF);
    int file = openat(dir, "moved", O_RDONLY);
    CHECK(fstatat(file, "x", &st, 0) == -1 && errno == ENOTDIR);
    close(file);
    CHECK(fstatat(dir, "", &st, 0) == -1 && errno == ENOENT);
    CHECK(unlinkat(dir, "sub", 0) == -1);

    // fchown on an open file (libctest runs as root)
    file = openat(dir, "moved", O_RDONLY);
    CHECK(fchown(file, 1000, 1000) == 0);
    CHECK(fstat(file, &st) == 0 && st.st_uid == 1000 && st.st_gid == 1000);
    close(file);

    CHECK(unlinkat(dir, "moved", 0) == 0);
    close(sub);
    CHECK(unlinkat(dir, "sub", AT_REMOVEDIR) == 0);
    close(dir);
    CHECK(rmdir(top) == 0);
}

void test_files(void)
{
    test_at_calls();
}

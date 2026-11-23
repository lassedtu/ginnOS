/**
 * @file files.c
 * @brief libctest: the calls that name a file relative to an open
 *        directory (openat and the other *at calls), fdopendir, hard
 *        and symbolic links, and file times.
 */

#include "libctest.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <utime.h>
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

/** the contents of a small file, or "" */
static const char *read_all(const char *path)
{
    static char text[64];
    text[0] = '\0';
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return text;
    ssize_t n = read(fd, text, sizeof(text) - 1);
    text[n > 0 ? n : 0] = '\0';
    close(fd);
    return text;
}

/** links on the disk (ext2): hard and symbolic */
static void test_links(void)
{
    char top[] = "/libctest-ln.XXXXXX";
    CHECK(mkdtemp(top) != NULL);
    CHECK(chdir(top) == 0);
    mkdir("dir", 0755);
    int fd = open("dir/file", O_WRONLY | O_CREAT, 0644);
    CHECK(fd >= 0 && write(fd, "abc", 3) == 3);
    close(fd);

    // hard links: one file, two names
    struct stat st;
    CHECK(link("dir/file", "hard") == 0);
    CHECK(stat("hard", &st) == 0 && st.st_nlink == 2);
    struct stat st2;
    CHECK(stat("dir/file", &st2) == 0 && st2.st_ino == st.st_ino);
    CHECK(link("dir", "dirlink") == -1 && errno == EPERM);
    CHECK(link("hard", "dir/file") == -1 && errno == EEXIST);

    // symbolic links: to a file, to a directory, dangling, to itself
    CHECK(symlink("dir/file", "soft") == 0);
    CHECK(symlink("dir", "dirsoft") == 0);
    CHECK(symlink("nowhere", "dangling") == 0);
    CHECK(symlink("self", "self") == 0);
    CHECK(symlink("x", "soft") == -1 && errno == EEXIST);

    char buffer[64];
    ssize_t n = readlink("soft", buffer, sizeof(buffer));
    CHECK(n == 8 && memcmp(buffer, "dir/file", 8) == 0);
    CHECK(readlink("soft", buffer, 3) == 3); // cut, with no NUL
    CHECK(readlink("hard", buffer, sizeof(buffer)) == -1 && errno == EINVAL);

    CHECK(lstat("soft", &st) == 0 && S_ISLNK(st.st_mode) && st.st_size == 8);
    CHECK(stat("soft", &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 3);
    CHECK(strcmp(read_all("soft"), "abc") == 0);
    CHECK(strcmp(read_all("dirsoft/file"), "abc") == 0);
    CHECK(stat("dangling", &st) == -1 && errno == ENOENT);
    CHECK(lstat("dangling", &st) == 0);
    CHECK(stat("self", &st) == -1 && errno == ELOOP);

    // a directory through a link, and readdir's type
    int found = 0;
    DIR *dir = opendir("dirsoft");
    CHECK(dir != NULL);
    if (dir)
    {
        CHECK(count_entries(dir, "file", &found) == 3);
        closedir(dir);
    }
    CHECK(found);
    dir = opendir(".");
    int link_type = 0;
    struct dirent *d;
    while (dir && (d = readdir(dir)))
        if (strcmp(d->d_name, "soft") == 0)
            link_type = d->d_type == DT_LNK;
    if (dir)
        closedir(dir);
    CHECK(link_type);

    // writing through a link changes the target; unlink removes the link
    fd = open("soft", O_WRONLY | O_TRUNC);
    CHECK(fd >= 0 && write(fd, "xyz", 3) == 3);
    close(fd);
    CHECK(strcmp(read_all("hard"), "xyz") == 0);
    CHECK(unlink("soft") == 0);
    CHECK(stat("dir/file", &st) == 0 && st.st_nlink == 2);
    CHECK(unlink("dir/file") == 0);
    CHECK(stat("hard", &st) == 0 && st.st_nlink == 1);
    CHECK(strcmp(read_all("hard"), "xyz") == 0);

    // a target as long as a block can hold, nearly (not kept in the inode)
    char long_target[200];
    memset(long_target, 'a', sizeof(long_target) - 1);
    long_target[sizeof(long_target) - 1] = '\0';
    CHECK(symlink(long_target, "long") == 0);
    CHECK(readlink("long", buffer, sizeof(buffer)) == (ssize_t)sizeof(buffer));
    CHECK(lstat("long", &st) == 0 && st.st_size == 199);

    unlink("long");
    unlink("self");
    unlink("dangling");
    unlink("dirsoft");
    unlink("hard");
    CHECK(rmdir("dir") == 0);
    CHECK(chdir("/") == 0);
    CHECK(rmdir(top) == 0);
}

/** symbolic links in memory (tmpfs), which has no hard links */
static void test_tmpfs_links(void)
{
    char buffer[16];
    struct stat st;
    unlink("/tmp/libctest-soft");
    CHECK(symlink("/etc/passwd", "/tmp/libctest-soft") == 0);
    CHECK(readlink("/tmp/libctest-soft", buffer, sizeof(buffer)) == 11);
    CHECK(lstat("/tmp/libctest-soft", &st) == 0 && S_ISLNK(st.st_mode));
    // the target is on another filesystem
    CHECK(stat("/tmp/libctest-soft", &st) == 0 && S_ISREG(st.st_mode));
    CHECK(strncmp(read_all("/tmp/libctest-soft"), "root:", 5) == 0);
    CHECK(link("/tmp/libctest-soft", "/tmp/libctest-hard") == -1 && errno == EPERM);
    CHECK(unlink("/tmp/libctest-soft") == 0);
}

/** set and read back the times of a file in dir (ext2 or tmpfs) */
static void check_times(const char *dir)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/libctest-times", dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);

    // a new file has the time now
    struct stat st;
    time_t now = time(NULL);
    CHECK(fstat(fd, &st) == 0 && st.st_mtime >= now - 5 && st.st_mtime <= now + 5);
    CHECK(st.st_mtim.tv_sec == st.st_mtime && st.st_mtim.tv_nsec == 0);

    struct timespec times[2] = {{1000000000, 0}, {1200000000, 0}};
    CHECK(utimensat(AT_FDCWD, path, times, 0) == 0);
    CHECK(stat(path, &st) == 0 && st.st_atime == 1000000000 && st.st_mtime == 1200000000);

    // UTIME_OMIT keeps a time; UTIME_NOW takes the time now
    struct timespec omit_now[2] = {{0, UTIME_OMIT}, {0, UTIME_NOW}};
    CHECK(futimens(fd, omit_now) == 0);
    CHECK(fstat(fd, &st) == 0 && st.st_atime == 1000000000 && st.st_mtime >= now - 5);

    struct utimbuf old = {.actime = 86400, .modtime = 2 * 86400};
    CHECK(utime(path, &old) == 0);
    CHECK(stat(path, &st) == 0 && st.st_atime == 86400 && st.st_mtime == 2 * 86400);
    struct timeval tv[2] = {{300, 0}, {400, 999}};
    CHECK(utimes(path, tv) == 0);
    CHECK(stat(path, &st) == 0 && st.st_atime == 300 && st.st_mtime == 400);

    // writing moves the modification time on
    CHECK(write(fd, "x", 1) == 1);
    CHECK(fstat(fd, &st) == 0 && st.st_mtime >= now - 5);

    struct timespec bad[2] = {{0, 1000000000}, {0, 0}};
    CHECK(utimensat(AT_FDCWD, path, bad, 0) == -1 && errno == EINVAL);
    CHECK(utimensat(AT_FDCWD, "/no/such/file", NULL, 0) == -1 && errno == ENOENT);
    close(fd);
    CHECK(unlink(path) == 0);
}

/** ftruncate to a length, on ext2 or tmpfs */
static void check_truncate(const char *dir)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/libctest-cut", dir);
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);
    CHECK(write(fd, "0123456789", 10) == 10);
    CHECK(ftruncate(fd, 4) == 0);
    struct stat st;
    CHECK(fstat(fd, &st) == 0 && st.st_size == 4);
    CHECK(lseek(fd, 0, SEEK_CUR) == 10); // the offset stays
    CHECK(ftruncate(fd, 8) == 0);
    char buffer[8];
    CHECK(pread(fd, buffer, 8, 0) == 8);
    CHECK(memcmp(buffer, "0123\0\0\0\0", 8) == 0);
    CHECK(ftruncate(fd, -1) == -1 && errno == EINVAL);
    close(fd);
    CHECK(truncate(path, 2) == 0 && stat(path, &st) == 0 && st.st_size == 2);
    CHECK(unlink(path) == 0);
}

void test_files(void)
{
    check_truncate("");
    check_truncate("/tmp");
    check_times("");
    check_times("/tmp");
    test_at_calls();
    test_links();
    test_tmpfs_links();
}

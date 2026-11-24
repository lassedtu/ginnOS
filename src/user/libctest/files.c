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
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
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
    int pipe_fds[2];
    CHECK(pipe(pipe_fds) == 0);
    CHECK(fstatat(pipe_fds[0], "x", &st, 0) == -1 && errno == ENOTDIR);
    close(pipe_fds[0]);
    close(pipe_fds[1]);

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

/** flock: shared and exclusive locks between open files */
static void test_flock(void)
{
    const char *path = "/tmp/libctest-lock";
    int a = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int b = open(path, O_RDWR);
    CHECK(a >= 0 && b >= 0);

    CHECK(flock(a, LOCK_SH) == 0);
    CHECK(flock(b, LOCK_SH | LOCK_NB) == 0); // shared with shared
    CHECK(flock(b, LOCK_EX | LOCK_NB) == -1 && errno == EWOULDBLOCK);
    CHECK(flock(a, LOCK_UN) == 0);
    CHECK(flock(b, LOCK_EX | LOCK_NB) == 0); // now b alone
    CHECK(flock(a, LOCK_SH | LOCK_NB) == -1 && errno == EWOULDBLOCK);

    // a descriptor made by dup shares b's lock; the lock goes with the
    // last of them
    int c = dup(b);
    close(b);
    CHECK(flock(a, LOCK_EX | LOCK_NB) == -1 && errno == EWOULDBLOCK);
    close(c);
    CHECK(flock(a, LOCK_EX | LOCK_NB) == 0);
    CHECK(flock(a, 0) == -1 && errno == EINVAL);
    close(a);

    // a lock that waits: a child holds it for a moment
    int ready[2];
    CHECK(pipe(ready) == 0);
    pid_t pid = fork();
    if (pid == 0)
    {
        int fd = open(path, O_RDWR);
        flock(fd, LOCK_EX);
        write(ready[1], "x", 1);
        struct timespec moment = {0, 200 * 1000 * 1000};
        nanosleep(&moment, NULL);
        _exit(0); // the exit closes fd, which frees the lock
    }
    char x;
    CHECK(read(ready[0], &x, 1) == 1);
    a = open(path, O_RDWR);
    CHECK(flock(a, LOCK_EX | LOCK_NB) == -1 && errno == EWOULDBLOCK);
    CHECK(flock(a, LOCK_EX) == 0); // waits for the child's exit
    int status;
    CHECK(waitpid(pid, &status, WNOHANG) == pid || waitpid(pid, &status, 0) == pid);
    close(a);
    close(ready[0]);
    close(ready[1]);
    CHECK(unlink(path) == 0);

    int fds[2];
    CHECK(pipe(fds) == 0);
    CHECK(flock(fds[0], LOCK_EX) == -1 && errno == EINVAL);
    close(fds[0]);
    close(fds[1]);
}

/** a named pipe in dir: a child writes, the parent reads */
static void check_fifo(const char *dir)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/libctest-fifo", dir);
    unlink(path);
    CHECK(mkfifo(path, 0600) == 0);
    struct stat st;
    CHECK(stat(path, &st) == 0 && S_ISFIFO(st.st_mode) && (st.st_mode & 0777) == 0600);
    CHECK(mkfifo(path, 0600) == -1 && errno == EEXIST);

    // the writer's open waits for the reader's
    pid_t pid = fork();
    if (pid == 0)
    {
        int fd = open(path, O_WRONLY);
        _exit(fd >= 0 && write(fd, "hello", 5) == 5 ? 0 : 1);
    }
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0);
    char buffer[16];
    ssize_t total = 0;
    ssize_t n;
    while ((n = read(fd, buffer + total, sizeof(buffer) - (size_t)total)) > 0)
        total += n;
    CHECK(total == 5 && memcmp(buffer, "hello", 5) == 0); // then the end
    close(fd);
    int status;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);

    // no reader: a non-blocking writer fails; a non-blocking reader opens
    CHECK(open(path, O_WRONLY | O_NONBLOCK) == -1 && errno == ENXIO);
    int reader = open(path, O_RDONLY | O_NONBLOCK);
    CHECK(reader >= 0);
    int writer = open(path, O_WRONLY | O_NONBLOCK);
    CHECK(writer >= 0 && write(writer, "ab", 2) == 2);
    CHECK(read(reader, buffer, sizeof(buffer)) == 2);
    close(writer);
    close(reader);

    // both ends at once: no wait
    fd = open(path, O_RDWR);
    CHECK(fd >= 0 && write(fd, "xy", 2) == 2 && read(fd, buffer, 2) == 2);
    close(fd);
    CHECK(unlink(path) == 0);
}

/** device nodes: root makes them; outside /dev they have no device */
static void check_device_node(const char *dir)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/libctest-dev", dir);
    unlink(path);
    CHECK(mknod(path, S_IFCHR | 0600, makedev(300, 70000)) == 0);
    struct stat st;
    CHECK(stat(path, &st) == 0 && S_ISCHR(st.st_mode));
    CHECK(major(st.st_rdev) == 300 && minor(st.st_rdev) == 70000);
    CHECK(open(path, O_RDONLY) == -1 && errno == ENXIO);
    CHECK(unlink(path) == 0);
    CHECK(mknod(path, S_IFBLK | 0600, makedev(8, 1)) == 0);
    CHECK(stat(path, &st) == 0 && S_ISBLK(st.st_mode) && st.st_rdev == makedev(8, 1));
    CHECK(unlink(path) == 0);
    CHECK(mknod(path, S_IFDIR | 0700, 0) == -1 && errno == EINVAL);

    pid_t pid = fork();
    if (pid == 0)
        _exit(setuid(1000) == 0 && mknod("/tmp/libctest-u", S_IFCHR | 0600, 0) == -1 &&
                      errno == EPERM
                  ? 0
                  : 1);
    int status;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/** what a child sees after chroot: 0 when all is as it should be */
static int inside_chroot(const char *top)
{
    char cwd[64];
    char buffer[16];
    struct stat st;
    if (chroot(top) < 0 || chdir("/") < 0)
        return 1;
    if (!getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/") != 0)
        return 2;
    if (strcmp(read_all("/hello"), "inside") != 0)
        return 3;
    if (strcmp(read_all("/abs"), "inside") != 0) // an absolute link target
        return 4;
    if (stat("/../../hello", &st) < 0) // ".." stops at the root
        return 5;
    if (stat("/etc/passwd", &st) == 0 || errno != ENOENT)
        return 6;
    int dir = open("/sub", O_RDONLY | O_DIRECTORY);
    if (dir < 0 || fstatat(dir, "../hello", &st, 0) < 0)
        return 7;
    if (chdir("sub") < 0 || !getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/sub") != 0)
        return 8;
    if (readlink("/abs", buffer, sizeof(buffer)) != 6)
        return 9;
    char *own = getcwd(NULL, 0);
    if (!own || strcmp(own, "/sub") != 0)
        return 10;
    free(own);
    char *real = realpath("../hello", NULL);
    if (!real || strcmp(real, "/hello") != 0)
        return 11;
    free(real);
    return 0;
}

static void test_chroot(void)
{
    char top[] = "/libctest-root.XXXXXX";
    CHECK(mkdtemp(top) != NULL);
    char path[64];
    snprintf(path, sizeof(path), "%s/hello", top);
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    CHECK(fd >= 0 && write(fd, "inside", 6) == 6);
    close(fd);
    snprintf(path, sizeof(path), "%s/abs", top);
    CHECK(symlink("/hello", path) == 0);
    snprintf(path, sizeof(path), "%s/sub", top);
    CHECK(mkdir(path, 0755) == 0);

    pid_t pid = fork();
    if (pid == 0)
        _exit(inside_chroot(top));
    int status;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    if (WEXITSTATUS(status))
        printf("libctest: chroot: step %d failed\n", WEXITSTATUS(status));

    // the parent's root did not change; a user may not chroot
    struct stat st;
    CHECK(stat("/etc/passwd", &st) == 0);
    pid = fork();
    if (pid == 0)
        _exit(setuid(1000) == 0 && chroot(top) == -1 && errno == EPERM ? 0 : 1);
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);

    CHECK(rmdir(path) == 0);
    snprintf(path, sizeof(path), "%s/abs", top);
    unlink(path);
    snprintf(path, sizeof(path), "%s/hello", top);
    unlink(path);
    CHECK(rmdir(top) == 0);
}

void test_files(void)
{
    test_chroot();
    check_fifo("");
    check_fifo("/tmp");
    check_device_node("");
    check_device_node("/tmp");
    test_flock();
    check_truncate("");
    check_truncate("/tmp");
    check_times("");
    check_times("/tmp");
    test_at_calls();
    test_links();
    test_tmpfs_links();
}

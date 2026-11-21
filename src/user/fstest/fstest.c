/**
 * @file fstest.c
 * @brief Exercise the kernel's file syscalls.
 *
 * usage: fstest <mode>
 *   stat   stat and fstat of a file, a directory, a device, a pipe and the
 *          console
 *   fcntl  dup, F_DUPFD, F_GETFD/F_SETFD and F_GETFL/F_SETFL (O_NONBLOCK on
 *          a pipe)
 *   rename rename a file, access before and after
 *   pread  pread and pwrite leave the file offset alone
 *
 * each mode prints "fstest <mode>: ok" when it passes.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *mode_name;

/**
 * report a failed check and return 1.
 */
static int failed(const char *what)
{
    printf("fstest %s: %s (errno %d)\n", mode_name, what, errno);
    return 1;
}

static int ok(void)
{
    printf("fstest %s: ok\n", mode_name);
    return 0;
}

static int test_stat(void)
{
    const char *path = "/fstest.stat";
    struct stat st;

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    write(fd, "hello", 5);

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != 5 || st.st_ino == 0)
        return failed("stat of a regular file");

    struct stat fst;
    if (fstat(fd, &fst) != 0 || fst.st_ino != st.st_ino || fst.st_size != 5)
        return failed("fstat of the same file");
    close(fd);
    unlink(path);

    if (stat("/bin", &st) != 0 || !S_ISDIR(st.st_mode))
        return failed("stat of a directory");
    if (stat("/dev/fb0", &st) != 0 || !S_ISCHR(st.st_mode))
        return failed("stat of a character device");
    if (stat("/dev/hda", &st) != 0 || !S_ISBLK(st.st_mode))
        return failed("stat of a block device");
    if (stat("/no/such/file", &st) != -1 || errno != ENOENT)
        return failed("stat of a missing file");

    int fds[2];
    pipe(fds);
    if (fstat(fds[0], &st) != 0 || !S_ISFIFO(st.st_mode))
        return failed("fstat of a pipe");
    close(fds[0]);
    close(fds[1]);

    if (fstat(1, &st) != 0 || !S_ISCHR(st.st_mode))
        return failed("fstat of the console");

    return ok();
}

static int test_fcntl(void)
{
    int fds[2];
    pipe(fds);

    // dup and F_DUPFD give the lowest free descriptor at or above the limit
    int copy = dup(fds[1]);
    int high = fcntl(fds[1], F_DUPFD, 12);
    if (copy < 0 || high != 12)
        return failed("dup / F_DUPFD");

    if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) != 0 || fcntl(fds[0], F_GETFD) != FD_CLOEXEC)
        return failed("F_SETFD / F_GETFD");
    int cloexec_copy = fcntl(fds[0], F_DUPFD_CLOEXEC, 0);
    if (fcntl(cloexec_copy, F_GETFD) != FD_CLOEXEC || fcntl(dup(fds[0]), F_GETFD) != 0)
        return failed("F_DUPFD_CLOEXEC, or dup keeping FD_CLOEXEC");

    if ((fcntl(fds[0], F_GETFL) & O_ACCMODE) != O_RDONLY ||
        (fcntl(fds[1], F_GETFL) & O_ACCMODE) != O_WRONLY)
        return failed("F_GETFL access modes of a pipe");

    // the status flags belong to the open file: the copy sees them too
    char byte;
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    if (read(cloexec_copy, &byte, 1) != -1 || errno != EAGAIN)
        return failed("O_NONBLOCK set with F_SETFL");

    if (fcntl(99, F_GETFD) != -1 || errno != EBADF)
        return failed("fcntl on a closed descriptor");

    return ok();
}

static int test_rename(void)
{
    const char *from = "/fstest.from";
    const char *to = "/fstest.to";

    close(open(from, O_WRONLY | O_CREAT | O_TRUNC, 0644));
    if (access(from, F_OK) != 0 || access(to, F_OK) != -1 || errno != ENOENT)
        return failed("access before the rename");

    if (rename(from, to) != 0)
        return failed("rename");
    if (access(from, F_OK) != -1 || access(to, R_OK | W_OK) != 0)
        return failed("access after the rename");
    unlink(to);

    if (rename("/bin/cat", "/dev/cat") != -1 || errno != EXDEV)
        return failed("rename across filesystems");

    return ok();
}

static int test_pread(void)
{
    const char *path = "/fstest.pread";
    char buf[8] = {0};

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    write(fd, "abcdef", 6);
    lseek(fd, 1, SEEK_SET);

    if (pread(fd, buf, 3, 2) != 3 || strcmp(buf, "cde") != 0)
        return failed("pread");
    if (pwrite(fd, "XY", 2, 4) != 2)
        return failed("pwrite");

    // the offset is still 1
    memset(buf, 0, sizeof(buf));
    if (read(fd, buf, 7) != 5 || strcmp(buf, "bcdXY") != 0)
        return failed("the offset moved");

    int fds[2];
    pipe(fds);
    if (pread(fds[0], buf, 1, 0) != -1 || errno != ESPIPE)
        return failed("pread on a pipe");

    if (fsync(fd) != 0)
        return failed("fsync");

    close(fd);
    unlink(path);
    return ok();
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: fstest stat|fcntl|rename|pread\n");
        return 1;
    }

    mode_name = argv[1];
    if (strcmp(argv[1], "stat") == 0)
        return test_stat();
    if (strcmp(argv[1], "fcntl") == 0)
        return test_fcntl();
    if (strcmp(argv[1], "rename") == 0)
        return test_rename();
    if (strcmp(argv[1], "pread") == 0)
        return test_pread();

    printf("fstest: unknown mode %s\n", argv[1]);
    return 1;
}

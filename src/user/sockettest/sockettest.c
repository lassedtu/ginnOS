/**
 * @file sockettest.c
 * @brief test Unix domain sockets (socket.h, un.h).
 *
 * usage: sockettest
 *
 * checks, in order:
 *   pair    socketpair: data both ways, read and write work too
 *   end     after the peer closes: recv gives 0, send gives EPIPE, and
 *           write without MSG_NOSIGNAL sends SIGPIPE
 *   named   bind, listen, connect, accept (the server waits in accept
 *           before the client connects); EADDRINUSE, ECONNREFUSED, ENOTSOCK
 *   rights  a child sends a shared memory fd; the parent maps it, and the
 *           writes of each are seen by the other
 *   bounds  two messages with files: each recvmsg stops at the next one
 *   ctrunc  files that do not fit in msg_control are closed (MSG_CTRUNC)
 *   nonblock SOCK_NONBLOCK and MSG_DONTWAIT give EAGAIN; poll reports
 *           POLLIN, POLLOUT and POLLHUP
 *   free    the checks run in a child 4 times, and free memory is the same
 *           after runs 2, 3 and 4
 *
 * prints "sockettest: ok" when all pass.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define SOCKET_NAME "/tmp/sockettest"

static int failed(const char *what)
{
    printf("sockettest: %s failed (errno %d)\n", what, errno);
    return 1;
}

/**
 * free memory in kB, from /proc/meminfo.
 */
static long free_kb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return -1;
    char line[128];
    long value = -1;
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "MemFree:", 8) == 0)
        {
            value = atol(line + 8);
            break;
        }
    }
    fclose(f);
    return value;
}

static socklen_t address(struct sockaddr_un *addr, const char *path)
{
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    strcpy(addr->sun_path, path);
    return (socklen_t)(sizeof(addr->sun_family) + strlen(path) + 1);
}

/**
 * wait for a child.
 * @return its exit status, or -1 if it did not exit normally.
 */
static int wait_child(pid_t pid)
{
    int status;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}

/**
 * send bytes and the fds in files (count of them may be 0).
 */
static ssize_t send_fds(int fd, const void *data, size_t length, const int *files, int count)
{
    struct iovec iov = {(void *)data, length};
    struct msghdr msg = {0};
    unsigned char control[CMSG_SPACE(4 * sizeof(int))];
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (count > 0)
    {
        memset(control, 0, sizeof(control));
        msg.msg_control = control;
        msg.msg_controllen = CMSG_SPACE(count * sizeof(int));
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(count * sizeof(int));
        memcpy(CMSG_DATA(cmsg), files, count * sizeof(int));
    }
    return sendmsg(fd, &msg, 0);
}

/**
 * receive bytes and up to room passed fds.
 * @param count receives the number of fds.
 * @param flags receives msg_flags.
 */
static ssize_t recv_fds(int fd, void *data, size_t length, int *files, int room, int *count,
                        int *flags)
{
    struct iovec iov = {data, length};
    struct msghdr msg = {0};
    unsigned char control[CMSG_SPACE(4 * sizeof(int))];
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (room > 0)
    {
        msg.msg_control = control;
        msg.msg_controllen = CMSG_SPACE(room * sizeof(int));
    }
    *count = 0;
    ssize_t n = recvmsg(fd, &msg, 0);
    if (n < 0)
        return n;
    *flags = msg.msg_flags;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
    {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
        {
            int n_fds = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            memcpy(files + *count, CMSG_DATA(c), n_fds * sizeof(int));
            *count += n_fds;
        }
    }
    return n;
}

static int check_pair(void)
{
    int sv[2];
    char buf[32];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return failed("socketpair");
    if (send(sv[0], "ping", 4, 0) != 4 || recv(sv[1], buf, sizeof(buf), 0) != 4 ||
        memcmp(buf, "ping", 4) != 0)
        return failed("pair: one way");
    if (write(sv[1], "pong!", 5) != 5 || read(sv[0], buf, sizeof(buf)) != 5 ||
        memcmp(buf, "pong!", 5) != 0)
        return failed("pair: the other way, with read and write");

    // end: the peer closes
    if (send(sv[0], "last", 4, 0) != 4)
        return failed("end: send before close");
    close(sv[0]);
    if (recv(sv[1], buf, sizeof(buf), 0) != 4 || recv(sv[1], buf, sizeof(buf), 0) != 0)
        return failed("end: the rest, then 0");
    errno = 0;
    if (send(sv[1], "x", 1, MSG_NOSIGNAL) != -1 || errno != EPIPE)
        return failed("end: EPIPE");

    // write without MSG_NOSIGNAL ends the writer with SIGPIPE
    pid_t pid = fork();
    if (pid == 0)
    {
        write(sv[1], "x", 1);
        _exit(0);
    }
    int status;
    if (waitpid(pid, &status, 0) != pid || !WIFSIGNALED(status) || WTERMSIG(status) != SIGPIPE)
        return failed("end: SIGPIPE");
    close(sv[1]);
    return 0;
}

static int check_named(void)
{
    struct sockaddr_un addr;
    socklen_t length = address(&addr, SOCKET_NAME);
    char buf[32];

    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0 || bind(server, (struct sockaddr *)&addr, length) != 0 || listen(server, 4) != 0)
        return failed("named: socket, bind, listen");

    int other = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    errno = 0;
    if (other < 0 || bind(other, (struct sockaddr *)&addr, length) == 0 || errno != EADDRINUSE)
        return failed("named: EADDRINUSE");
    struct sockaddr_un nobody;
    socklen_t nobody_length = address(&nobody, "/tmp/nobody-listens");
    errno = 0;
    if (connect(other, (struct sockaddr *)&nobody, nobody_length) == 0 || errno != ECONNREFUSED)
        return failed("named: ECONNREFUSED");
    close(other);
    errno = 0;
    if (listen(0, 1) == 0 || errno != ENOTSOCK)
        return failed("named: ENOTSOCK");

    // the client connects after a while, so the server waits in accept
    pid_t pid = fork();
    if (pid == 0)
    {
        close(server);
        usleep(100 * 1000);
        int c = socket(AF_UNIX, SOCK_STREAM, 0);
        if (c < 0 || connect(c, (struct sockaddr *)&addr, length) != 0)
            _exit(1);
        if (send(c, "hello server", 12, 0) != 12)
            _exit(2);
        if (recv(c, buf, sizeof(buf), 0) != 12 || memcmp(buf, "hello client", 12) != 0)
            _exit(3);
        _exit(0);
    }
    struct sockaddr_un peer;
    socklen_t peer_length = sizeof(peer);
    int conn = accept(server, (struct sockaddr *)&peer, &peer_length);
    if (conn < 0 || peer_length != sizeof(sa_family_t) || peer.sun_family != AF_UNIX)
        return failed("named: accept");
    if (recv(conn, buf, sizeof(buf), 0) != 12 || memcmp(buf, "hello server", 12) != 0)
        return failed("named: data from the client");
    if (send(conn, "hello client", 12, 0) != 12)
        return failed("named: data to the client");
    if (wait_child(pid) != 0)
        return failed("named: the client");
    if (recv(conn, buf, sizeof(buf), 0) != 0)
        return failed("named: the end after the client exits");
    close(conn);

    // the name is free again after close
    close(server);
    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0 || bind(server, (struct sockaddr *)&addr, length) != 0)
        return failed("named: bind after close");
    close(server);
    return 0;
}

static int check_rights(void)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return failed("rights: socketpair");

    pid_t pid = fork();
    if (pid == 0)
    {
        close(sv[0]);
        int shm = shm_create(4096, 0);
        char *mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
        if (shm < 0 || mem == MAP_FAILED)
            _exit(1);
        strcpy(mem, "from the child");
        if (send_fds(sv[1], "B", 1, &shm, 1) != 1)
            _exit(2);
        close(shm);
        char ack;
        if (recv(sv[1], &ack, 1, 0) != 1 || strcmp(mem + 100, "from the parent") != 0)
            _exit(3);
        _exit(0);
    }
    close(sv[1]);

    char byte;
    int files[4];
    int count;
    int flags;
    if (recv_fds(sv[0], &byte, 1, files, 4, &count, &flags) != 1 || byte != 'B' || count != 1)
        return failed("rights: receive the fd");
    char *mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, files[0], 0);
    if (mem == MAP_FAILED || strcmp(mem, "from the child") != 0)
        return failed("rights: map the received fd");
    strcpy(mem + 100, "from the parent");
    if (send(sv[0], "!", 1, 0) != 1 || wait_child(pid) != 0)
        return failed("rights: the child sees the parent's write");
    munmap(mem, 4096);
    close(files[0]);
    close(sv[0]);
    return 0;
}

static int check_bounds(void)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return failed("bounds: socketpair");
    int p[2];
    if (pipe(p) != 0)
        return failed("bounds: pipe");

    // two messages, each with files, and plain data between them
    if (send_fds(sv[0], "one", 3, &p[0], 1) != 3 || send(sv[0], "-", 1, 0) != 1 ||
        send_fds(sv[0], "two", 3, p, 2) != 3)
        return failed("bounds: send");

    char buf[32];
    int files[4];
    int count;
    int flags;
    ssize_t n = recv_fds(sv[1], buf, sizeof(buf), files, 4, &count, &flags);
    if (n != 4 || memcmp(buf, "one-", 4) != 0 || count != 1)
        return failed("bounds: the first message");
    close(files[0]);
    n = recv_fds(sv[1], buf, sizeof(buf), files, 4, &count, &flags);
    if (n != 3 || memcmp(buf, "two", 3) != 0 || count != 2)
        return failed("bounds: the second message");

    // the received pipe ends work
    if (write(files[1], "z", 1) != 1 || read(files[0], buf, 1) != 1 || buf[0] != 'z')
        return failed("bounds: the received pipe");
    close(files[0]);
    close(files[1]);

    // ctrunc: no room for the files
    if (send_fds(sv[0], "c", 1, p, 2) != 1)
        return failed("ctrunc: send");
    n = recv_fds(sv[1], buf, sizeof(buf), files, 0, &count, &flags);
    if (n != 1 || count != 0 || !(flags & MSG_CTRUNC))
        return failed("ctrunc: MSG_CTRUNC");

    // a message with files and no data is refused
    errno = 0;
    if (send_fds(sv[0], "", 0, p, 1) != -1 || errno != EINVAL)
        return failed("bounds: files without data");

    close(p[0]);
    close(p[1]);
    close(sv[0]);
    close(sv[1]);
    return 0;
}

static int check_nonblock(void)
{
    int sv[2];
    char buf[1024];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv) != 0)
        return failed("nonblock: socketpair");
    errno = 0;
    if (recv(sv[0], buf, sizeof(buf), 0) != -1 || errno != EAGAIN)
        return failed("nonblock: recv gives EAGAIN");

    struct pollfd pfd = {sv[0], POLLIN | POLLOUT, 0};
    if (poll(&pfd, 1, 0) != 1 || pfd.revents != POLLOUT)
        return failed("nonblock: poll, only POLLOUT");
    send(sv[1], "x", 1, 0);
    pfd.revents = 0;
    if (poll(&pfd, 1, 0) != 1 || pfd.revents != (POLLIN | POLLOUT))
        return failed("nonblock: poll, POLLIN");

    // fill the other way until it is full
    memset(buf, 'f', sizeof(buf));
    long total = 0;
    for (;;)
    {
        ssize_t n = send(sv[0], buf, sizeof(buf), 0);
        if (n < 0)
            break;
        total += n;
    }
    if (errno != EAGAIN || total == 0)
        return failed("nonblock: send gives EAGAIN when full");
    pfd.revents = 0;
    if (poll(&pfd, 1, 0) != 1 || pfd.revents != POLLIN)
        return failed("nonblock: poll, no POLLOUT when full");

    // MSG_DONTWAIT on a blocking socket
    int bv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, bv) != 0)
        return failed("nonblock: socketpair");
    errno = 0;
    if (recv(bv[0], buf, 1, MSG_DONTWAIT) != -1 || errno != EAGAIN)
        return failed("nonblock: MSG_DONTWAIT");

    // POLLHUP when the peer is gone
    close(bv[1]);
    struct pollfd hup = {bv[0], POLLIN, 0};
    if (poll(&hup, 1, 0) != 1 || !(hup.revents & POLLHUP))
        return failed("nonblock: POLLHUP");
    close(bv[0]);
    close(sv[0]);
    close(sv[1]);
    return 0;
}

static int run_checks(void)
{
    if (check_pair() || check_named() || check_rights() || check_bounds() || check_nonblock())
        return 1;
    return 0;
}

int main(void)
{
    long free_after[4];
    for (int run = 0; run < 4; run++)
    {
        pid_t pid = fork();
        if (pid < 0)
            return failed("fork");
        if (pid == 0)
            _exit(run_checks());
        if (wait_child(pid) != 0)
            return 1; // the child printed what failed
        free_after[run] = free_kb();
    }
    if (free_after[1] < 0 || free_after[2] != free_after[1] || free_after[3] != free_after[1])
    {
        printf("sockettest: free memory after each run: %ld %ld %ld %ld kB\n", free_after[0],
               free_after[1], free_after[2], free_after[3]);
        return failed("freeing the sockets");
    }

    printf("sockettest: ok\n");
    return 0;
}

#include "poll.h"

#include "errno.h"
#include "syscall.h"
#include "sys/select.h"

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    int ret = _syscall(SYS_POLL, (int)fds, (int)nfds, timeout, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

/**
 * true if fd is in a set (a NULL set holds nothing).
 */
static int in_set(const fd_set *set, int fd)
{
    return set && FD_ISSET(fd, set);
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
    if (nfds < 0 || nfds > FD_SETSIZE)
    {
        errno = EINVAL;
        return -1;
    }

    // one pollfd per descriptor in any set
    struct pollfd fds[FD_SETSIZE];
    int count = 0;
    for (int fd = 0; fd < nfds; fd++)
    {
        short events = 0;
        if (in_set(readfds, fd))
            events |= POLLIN;
        if (in_set(writefds, fd))
            events |= POLLOUT;
        if (events || in_set(exceptfds, fd))
        {
            fds[count].fd = fd;
            fds[count].events = events;
            count++;
        }
    }

    int ms = -1;
    if (timeout)
    {
        ms = (int)(timeout->tv_sec * 1000 + (timeout->tv_usec + 999) / 1000);
    }

    if (poll(fds, (nfds_t)count, ms) < 0)
    {
        return -1;
    }

    // rebuild the sets from the results; a hang-up or error counts as
    // readable (a read returns at once), as POSIX select reports it
    int ready = 0;
    for (int i = 0; i < count; i++)
    {
        int fd = fds[i].fd;
        short revents = fds[i].revents;
        if (revents & POLLNVAL)
        {
            errno = EBADF;
            return -1;
        }

        int readable = in_set(readfds, fd) && (revents & (POLLIN | POLLHUP | POLLERR));
        int writable = in_set(writefds, fd) && (revents & (POLLOUT | POLLERR));
        int failed = in_set(exceptfds, fd) && (revents & POLLPRI);

        if (readfds)
            readable ? (void)FD_SET(fd, readfds) : (void)FD_CLR(fd, readfds);
        if (writefds)
            writable ? (void)FD_SET(fd, writefds) : (void)FD_CLR(fd, writefds);
        if (exceptfds)
            failed ? (void)FD_SET(fd, exceptfds) : (void)FD_CLR(fd, exceptfds);
        ready += readable + writable + failed;
    }
    return ready;
}

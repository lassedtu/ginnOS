/**
 * @file socket.c
 * @brief the socket calls (sys/socket.h): thin wrappers of the syscalls.
 */

#include <errno.h>
#include <sys/socket.h>
#include <syscall.h>

static long result(long ret)
{
    if (ret < 0)
    {
        errno = (int)-ret;
        return -1;
    }
    return ret;
}

int socket(int domain, int type, int protocol)
{
    return (int)result(_syscall(SYS_SOCKET, domain, type, protocol, 0, 0));
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    return (int)result(_syscall(SYS_SOCKETPAIR, domain, type, protocol, (long)sv, 0));
}

int bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    return (int)result(_syscall(SYS_BIND, fd, (long)addr, (long)addrlen, 0, 0));
}

int listen(int fd, int backlog)
{
    return (int)result(_syscall(SYS_LISTEN, fd, backlog, 0, 0, 0));
}

int accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    return (int)result(_syscall(SYS_ACCEPT, fd, (long)addr, (long)addrlen, flags, 0));
}

int accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    return accept4(fd, addr, addrlen, 0);
}

int connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    return (int)result(_syscall(SYS_CONNECT, fd, (long)addr, (long)addrlen, 0, 0));
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
    return result(_syscall(SYS_SENDMSG, fd, (long)msg, flags, 0, 0));
}

ssize_t recvmsg(int fd, struct msghdr *msg, int flags)
{
    return result(_syscall(SYS_RECVMSG, fd, (long)msg, flags, 0, 0));
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
    struct iovec iov = {(void *)buf, len};
    struct msghdr msg = {0};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    return sendmsg(fd, &msg, flags);
}

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
    struct iovec iov = {buf, len};
    struct msghdr msg = {0};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    return recvmsg(fd, &msg, flags);
}

struct cmsghdr *__cmsg_nxthdr(struct msghdr *msg, struct cmsghdr *cmsg)
{
    unsigned char *next = (unsigned char *)cmsg + CMSG_ALIGN(cmsg->cmsg_len);
    unsigned char *end = (unsigned char *)msg->msg_control + msg->msg_controllen;
    if (cmsg->cmsg_len < sizeof(struct cmsghdr) || next + sizeof(struct cmsghdr) > end)
    {
        return (struct cmsghdr *)0;
    }
    return (struct cmsghdr *)next;
}

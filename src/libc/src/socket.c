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

ssize_t sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to,
               socklen_t tolen)
{
    struct iovec iov = {(void *)buf, len};
    struct msghdr msg = {0};
    msg.msg_name = (void *)to;
    msg.msg_namelen = to ? tolen : 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    return sendmsg(fd, &msg, flags);
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from,
                 socklen_t *fromlen)
{
    struct iovec iov = {buf, len};
    struct msghdr msg = {0};
    msg.msg_name = from;
    msg.msg_namelen = from && fromlen ? *fromlen : 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    ssize_t n = recvmsg(fd, &msg, flags);
    if (n >= 0 && from && fromlen)
    {
        *fromlen = msg.msg_namelen;
    }
    return n;
}

int getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    return (int)result(_syscall(SYS_GETSOCKNAME, fd, (long)addr, (long)addrlen, 0, 0));
}

int getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    return (int)result(_syscall(SYS_GETPEERNAME, fd, (long)addr, (long)addrlen, 0, 0));
}

int setsockopt(int fd, int level, int name, const void *value, socklen_t length)
{
    return (int)result(_syscall(SYS_SETSOCKOPT, fd, level, name, (long)value, (long)length));
}

int getsockopt(int fd, int level, int name, void *value, socklen_t *length)
{
    return (int)result(_syscall(SYS_GETSOCKOPT, fd, level, name, (long)value, (long)length));
}

int shutdown(int fd, int how)
{
    return (int)result(_syscall(SYS_SHUTDOWN, fd, how, 0, 0, 0));
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

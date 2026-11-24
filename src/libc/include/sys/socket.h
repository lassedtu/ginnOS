#pragma once

#include <sys/types.h>
#include <sys/uio.h>

/**
 * @file sys/socket.h
 * @brief sockets: Unix domain stream sockets, with file descriptor
 *        passing, and IPv4 sockets (TCP; UDP, and ICMP for ping).
 *
 * AF_UNIX with SOCK_STREAM, and AF_INET with SOCK_STREAM or SOCK_DGRAM
 * (<netinet/in.h> has the addresses). the values and layouts are the same as Linux's,
 * and must match src/kernel/ipc/unix_socket.h and
 * src/kernel/net/inet_socket.h.
 *
 * to pass open files, put their fds in an SCM_RIGHTS control message of
 * sendmsg; the message must have at least one byte of data. the receiver
 * gets new fds for the same open files from recvmsg.
 */

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

struct sockaddr
{
    sa_family_t sa_family;
    char sa_data[14];
};

// a buffer large enough for any socket address
struct sockaddr_storage
{
    sa_family_t ss_family;
    char __ss_padding[128 - sizeof(sa_family_t) - sizeof(unsigned long)];
    unsigned long __ss_align;
};

#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_LOCAL  AF_UNIX
#define AF_INET   2
#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX   AF_UNIX
#define PF_LOCAL  AF_UNIX
#define PF_INET   AF_INET

#define SOCK_STREAM   1
#define SOCK_DGRAM    2
#define SOCK_NONBLOCK 0x00000800 // with the type: O_NONBLOCK on the new socket
#define SOCK_CLOEXEC  0x00080000 // with the type: FD_CLOEXEC on the new fd

#define SOL_SOCKET 1
#define SCM_RIGHTS 1

// options at SOL_SOCKET (int values)
#define SO_REUSEADDR 2
#define SO_TYPE      3
#define SO_ERROR     4
#define SO_BROADCAST 6
#define SO_SNDBUF    7
#define SO_RCVBUF    8
#define SO_KEEPALIVE 9

// how for shutdown
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define MSG_PEEK         0x0002     // recv: leave the data in the queue
#define MSG_CTRUNC       0x0008     // recvmsg: not all passed files fitted
#define MSG_TRUNC        0x0020     // recv: the datagram was longer than the buffer
#define MSG_DONTWAIT     0x0040     // this call does not block
#define MSG_NOSIGNAL     0x4000     // no SIGPIPE when the peer is gone
#define MSG_CMSG_CLOEXEC 0x40000000 // recvmsg: FD_CLOEXEC on the received fds

struct msghdr
{
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct cmsghdr
{
    size_t cmsg_len; // the header and the data, without the padding after it
    int cmsg_level;
    int cmsg_type;
};

#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len)   (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_DATA(cmsg) ((unsigned char *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_FIRSTHDR(msg)                                                                         \
    ((msg)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *)(msg)->msg_control      \
                                                     : (struct cmsghdr *)0)
#define CMSG_NXTHDR(msg, cmsg) __cmsg_nxthdr((msg), (cmsg))

/**
 * the control message after cmsg, or NULL. use CMSG_NXTHDR.
 */
struct cmsghdr *__cmsg_nxthdr(struct msghdr *msg, struct cmsghdr *cmsg);

int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int sv[2]);
int bind(int fd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *addr, socklen_t *addrlen);
int accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags);
int connect(int fd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t sendmsg(int fd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int fd, struct msghdr *msg, int flags);
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);
ssize_t sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to,
               socklen_t tolen);
ssize_t recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from,
                 socklen_t *fromlen);
int getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen);
int setsockopt(int fd, int level, int name, const void *value, socklen_t length);
int getsockopt(int fd, int level, int name, void *value, socklen_t *length);
int shutdown(int fd, int how);

#pragma once

/**
 * @file unix_socket.h
 * @brief Unix domain stream sockets, with file descriptor passing.
 *
 * a server binds a socket to a name and listens; a client connects to
 * the name and gets a connected pair. each side writes into the other's
 * receive buffer. sendmsg can pass open files (SCM_RIGHTS): they travel
 * with the first byte of the message, and recvmsg installs them as new
 * fds in the receiver. this is how an app hands its window buffer (a
 * shared memory fd) to the compositor.
 *
 * the names are kept in a kernel table, not as files in the filesystem:
 * any path works as a name, and nothing shows up in the directory.
 *
 * the layouts below are the user ABI (<sys/socket.h>, <sys/un.h>,
 * <sys/uio.h> in libc); they are the same as Linux's.
 */

#include "common/error.h"
#include "common/stdint.h"

struct file;

#define AF_UNIX        1
#define SOCK_STREAM    1
#define SOCK_NONBLOCK  0x00000800 // with the type: O_NONBLOCK on the new socket
#define SOCK_CLOEXEC   0x00080000 // with the type: FD_CLOEXEC on the new fd

#define SOL_SOCKET     1
#define SCM_RIGHTS     1

#define MSG_CTRUNC     0x0008 // recvmsg: not all passed files fitted
#define MSG_DONTWAIT   0x0040 // this call does not block
#define MSG_NOSIGNAL   0x4000 // sendmsg: no SIGPIPE when the peer is gone
#define MSG_CMSG_CLOEXEC 0x40000000 // recvmsg: FD_CLOEXEC on the received fds

#define UNIX_PATH_MAX  108
#define SOCKET_MAX_FDS 16 // files per message

typedef struct
{
    uint16_t sun_family;
    char sun_path[UNIX_PATH_MAX];
} ksockaddr_un_t;

typedef struct
{
    uintptr_t base;
    size_t len;
} kiovec_t;

typedef struct
{
    uintptr_t name;
    uint32_t namelen;
    uintptr_t iov;
    size_t iovlen;
    uintptr_t control;
    size_t controllen;
    int32_t flags;
} kmsghdr_t;

typedef struct
{
    size_t len;
    int32_t level;
    int32_t type;
} kcmsghdr_t;

typedef struct unix_socket unix_socket_t;

/**
 * make a new, unconnected socket and its open file.
 * @param flags O_NONBLOCK or 0.
 */
kerr_t unix_socket_create(uint32_t flags, struct file **out);

/**
 * make two connected sockets (socketpair).
 */
kerr_t unix_socket_pair(uint32_t flags, struct file **a, struct file **b);

/**
 * the socket of an open file, or NULL if the file is not a socket.
 */
unix_socket_t *unix_socket_of(struct file *file);

/**
 * give a socket a name. the name must not be in use.
 * @return 0, or -EADDRINUSE, -EINVAL.
 */
int32_t unix_socket_bind(unix_socket_t *socket, const char *path);

/**
 * let a bound socket take connections.
 * @return 0, or -EINVAL (not bound, or connected).
 */
int32_t unix_socket_listen(unix_socket_t *socket, uint32_t backlog);

/**
 * connect to the listening socket with this name.
 * @return 0, or -ECONNREFUSED, -EISCONN.
 */
int32_t unix_socket_connect(unix_socket_t *socket, const char *path);

/**
 * take the next connection of a listening socket (blocks unless the file
 * is non-blocking).
 * @param flags O_NONBLOCK or 0, for the new socket's file.
 * @param out receives the open file of the new connection.
 * @return 0, or -EINVAL, -EAGAIN, -ERESTARTSYS, -ENOMEM.
 */
int32_t unix_socket_accept(struct file *listener, uint32_t flags, struct file **out);

/**
 * send data from user memory, with files to pass (may be 0 of them).
 * @param files the files (the socket takes a reference to each).
 * @param dontwait do not block, as if the file were non-blocking.
 * @return the bytes sent, or -EPIPE, -ENOTCONN, -EAGAIN, -ERESTARTSYS.
 */
int32_t unix_socket_send(struct file *file, const kiovec_t *iov, uint32_t iov_count,
                         struct file **files, uint32_t file_count, bool dontwait);

/**
 * receive data into user memory, and the files passed with its first
 * byte. a call stops before the first byte of another message that
 * passes files, so each message's files come with their own data.
 * @param files receives the passed files (the caller owns the references).
 * @param file_count receives their number (0 if none).
 * @return the bytes received (0: the peer is gone and all is read), or
 *         -ENOTCONN, -EAGAIN, -ERESTARTSYS.
 */
int32_t unix_socket_receive(struct file *file, const kiovec_t *iov, uint32_t iov_count,
                            struct file **files, uint32_t *file_count, bool dontwait);

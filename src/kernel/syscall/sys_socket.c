/**
 * the socket syscalls: Unix domain stream sockets (kernel/ipc/unix_socket.h).
 *
 * the user structs (sockaddr_un, msghdr, iovec, cmsghdr) have the Linux
 * layouts. this file copies them in, checks the user ranges, and turns
 * fds into open files and back; the socket code does the rest.
 *
 * a known limit: a socket passed over itself (or in a ring of sockets
 * that are only open in each other's queues) is never freed. Linux has a
 * collector for this; we do not.
 */

#include "syscall_internal.h"
#include "syscall.h"

#include "common/memory.h"
#include "fd_table.h"
#include "kernel/ipc/unix_socket.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"

#define MAX_IOV     16  // iovecs per call
#define MAX_CONTROL 256 // bytes of control data per sendmsg

// the size of a cmsghdr and of the data after it, rounded as CMSG_ALIGN does
#define CMSG_ALIGN_K(n) (((n) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_HDR_SIZE   CMSG_ALIGN_K(sizeof(kcmsghdr_t))

/**
 * the socket of an fd of the current process.
 * @param err receives -EBADF or -ENOTSOCK when there is none.
 */
static unix_socket_t *socket_of_fd(int fd, int32_t *err)
{
    fd_entry_t *entry = fd_get(fd);
    if (!entry)
    {
        *err = -EBADF;
        return NULL;
    }
    unix_socket_t *s = unix_socket_of(entry->file);
    if (!s)
    {
        *err = -ENOTSOCK;
    }
    return s;
}

/**
 * copy a sockaddr_un in and take the path out of it.
 * @param path receives the path, terminated.
 * @return 0, or -EFAULT, -EINVAL, -EAFNOSUPPORT.
 */
static int32_t path_from_user(uintptr_t user_addr, uint32_t addrlen, char path[UNIX_PATH_MAX])
{
    ksockaddr_un_t addr;
    if (addrlen <= sizeof(addr.sun_family) || addrlen > sizeof(addr))
    {
        return -EINVAL;
    }
    memset(&addr, 0, sizeof(addr));
    if (kerr_failed(copy_from_user(&addr, (const void *)user_addr, addrlen)))
    {
        return -EFAULT;
    }
    if (addr.sun_family != AF_UNIX)
    {
        return -EAFNOSUPPORT;
    }
    uint32_t length = addrlen - sizeof(addr.sun_family);
    if (length >= UNIX_PATH_MAX)
    {
        length = UNIX_PATH_MAX - 1;
    }
    memcpy(path, addr.sun_path, length);
    path[length] = '\0';
    return path[0] == '\0' ? -EINVAL : 0;
}

/**
 * check the socket type argument.
 * @param flags receives the file flags (O_NONBLOCK) and fd flags (FD_CLOEXEC).
 */
static int32_t check_type(uint32_t domain, uint32_t type, uint32_t protocol, uint32_t *file_flags,
                          uint32_t *fd_flags)
{
    if (domain != AF_UNIX)
    {
        return -EAFNOSUPPORT;
    }
    if ((type & ~(uint32_t)(SOCK_NONBLOCK | SOCK_CLOEXEC)) != SOCK_STREAM || protocol != 0)
    {
        return -EPROTONOSUPPORT;
    }
    *file_flags = (type & SOCK_NONBLOCK) ? O_NONBLOCK : 0;
    *fd_flags = (type & SOCK_CLOEXEC) ? FD_CLOEXEC : 0;
    return 0;
}

/**
 * SYS_socket: make an unconnected socket.
 * args: domain (AF_UNIX), type (SOCK_STREAM, with SOCK_NONBLOCK and
 *       SOCK_CLOEXEC), protocol (0).
 * returns the new fd, or -EAFNOSUPPORT, -EPROTONOSUPPORT, -ENOMEM, -EMFILE.
 */
syscall_ret_t sys_socket(struct registers *regs)
{
    uint32_t file_flags;
    uint32_t fd_flags;
    int32_t err = check_type((uint32_t)syscall_arg(regs, 0), (uint32_t)syscall_arg(regs, 1),
                             (uint32_t)syscall_arg(regs, 2), &file_flags, &fd_flags);
    if (err != 0)
    {
        return err;
    }

    file_t *file;
    if (kerr_failed(unix_socket_create(file_flags, &file)))
    {
        return -ENOMEM;
    }
    int fd = fd_install(process_current(), file, fd_flags, 0);
    if (fd < 0)
    {
        file_unref(file);
    }
    return fd;
}

/**
 * SYS_socketpair: make two connected sockets.
 * args: domain, type, protocol (as for socket), int sv[2].
 * returns 0, or a negative errno.
 */
syscall_ret_t sys_socketpair(struct registers *regs)
{
    int *user_fds = (int *)syscall_arg(regs, 3);
    uint32_t file_flags;
    uint32_t fd_flags;
    int32_t err = check_type((uint32_t)syscall_arg(regs, 0), (uint32_t)syscall_arg(regs, 1),
                             (uint32_t)syscall_arg(regs, 2), &file_flags, &fd_flags);
    if (err != 0)
    {
        return err;
    }
    // checked up front: the fds are installed before they are copied out
    if (!user_access_ok(user_fds, 2 * sizeof(int), true))
    {
        return -EFAULT;
    }

    file_t *a;
    file_t *b;
    if (kerr_failed(unix_socket_pair(file_flags, &a, &b)))
    {
        return -ENOMEM;
    }
    process_t *proc = process_current();
    int fds[2];
    fds[0] = fd_install(proc, a, fd_flags, 0);
    fds[1] = fds[0] < 0 ? -EMFILE : fd_install(proc, b, fd_flags, 0);
    if (fds[1] < 0)
    {
        if (fds[0] >= 0)
        {
            proc->files->fd[fds[0]].file = NULL;
        }
        file_unref(a);
        file_unref(b);
        return -EMFILE;
    }
    return kerr_to_errno(copy_to_user(user_fds, fds, sizeof(fds)));
}

/**
 * SYS_bind: give a socket a name.
 * args: fd, const struct sockaddr_un *, addrlen.
 * returns 0, or -EBADF, -ENOTSOCK, -EINVAL, -EADDRINUSE, -EFAULT.
 */
syscall_ret_t sys_bind(struct registers *regs)
{
    int32_t err;
    unix_socket_t *s = socket_of_fd((int)syscall_arg(regs, 0), &err);
    if (!s)
    {
        return err;
    }
    char path[UNIX_PATH_MAX];
    err = path_from_user(syscall_arg(regs, 1), (uint32_t)syscall_arg(regs, 2), path);
    if (err != 0)
    {
        return err;
    }
    return unix_socket_bind(s, path);
}

/**
 * SYS_listen: let a bound socket take connections.
 * args: fd, backlog (0 or more than 16 means 16).
 * returns 0, or -EBADF, -ENOTSOCK, -EINVAL.
 */
syscall_ret_t sys_listen(struct registers *regs)
{
    int32_t err;
    unix_socket_t *s = socket_of_fd((int)syscall_arg(regs, 0), &err);
    if (!s)
    {
        return err;
    }
    int32_t backlog = (int32_t)syscall_arg(regs, 1);
    return unix_socket_listen(s, backlog < 0 ? 0 : (uint32_t)backlog);
}

/**
 * SYS_connect: connect to the listening socket with a name.
 * args: fd, const struct sockaddr_un *, addrlen.
 * returns 0, or -ECONNREFUSED (no one listens there, or the backlog is
 * full), -EISCONN, -EBADF, -ENOTSOCK, -EINVAL, -EFAULT.
 */
syscall_ret_t sys_connect(struct registers *regs)
{
    int32_t err;
    unix_socket_t *s = socket_of_fd((int)syscall_arg(regs, 0), &err);
    if (!s)
    {
        return err;
    }
    char path[UNIX_PATH_MAX];
    err = path_from_user(syscall_arg(regs, 1), (uint32_t)syscall_arg(regs, 2), path);
    if (err != 0)
    {
        return err;
    }
    return unix_socket_connect(s, path);
}

/**
 * SYS_accept: take the next connection of a listening socket (accept4).
 * args: fd, struct sockaddr_un * (or NULL), socklen_t *addrlen (or NULL),
 *       flags (SOCK_NONBLOCK, SOCK_CLOEXEC).
 * the peer has no name, so only sun_family is filled in and *addrlen
 * becomes 2.
 * returns the new fd, or -EAGAIN, -EINVAL, -EBADF, -ENOTSOCK, -EFAULT,
 * -EMFILE, -ENOMEM.
 */
syscall_ret_t sys_accept(struct registers *regs)
{
    int fd = (int)syscall_arg(regs, 0);
    uintptr_t user_addr = syscall_arg(regs, 1);
    uint32_t *user_addrlen = (uint32_t *)syscall_arg(regs, 2);
    uint32_t flags = (uint32_t)syscall_arg(regs, 3);
    if (flags & ~(uint32_t)(SOCK_NONBLOCK | SOCK_CLOEXEC))
    {
        return -EINVAL;
    }

    uint32_t addrlen = 0;
    if (user_addr && user_addrlen)
    {
        if (kerr_failed(copy_from_user(&addrlen, user_addrlen, sizeof(addrlen))))
        {
            return -EFAULT;
        }
        if (!user_access_ok((void *)user_addr, addrlen < 2 ? addrlen : 2, true) ||
            !user_access_ok(user_addrlen, sizeof(addrlen), true))
        {
            return -EFAULT;
        }
    }

    fd_entry_t *entry = fd_get(fd);
    if (!entry)
    {
        return -EBADF;
    }
    file_t *file;
    int32_t err = unix_socket_accept(entry->file, (flags & SOCK_NONBLOCK) ? O_NONBLOCK : 0, &file);
    if (err != 0)
    {
        return err;
    }
    int new_fd = fd_install(process_current(), file, (flags & SOCK_CLOEXEC) ? FD_CLOEXEC : 0, 0);
    if (new_fd < 0)
    {
        file_unref(file); // the connection is closed; its client sees the end
        return new_fd;
    }

    if (user_addr && user_addrlen)
    {
        uint16_t family = AF_UNIX;
        uint32_t length = sizeof(family);
        if (addrlen >= sizeof(family))
        {
            copy_to_user((void *)user_addr, &family, sizeof(family));
        }
        copy_to_user(user_addrlen, &length, sizeof(length));
    }
    return new_fd;
}

/**
 * copy a msghdr and its iovecs in, and check the data ranges.
 * @param write true for recvmsg (the data buffers are written).
 * @return the number of iovecs, or a negative errno.
 */
static int32_t msghdr_from_user(uintptr_t user_msg, kmsghdr_t *msg, kiovec_t iov[MAX_IOV],
                                bool write)
{
    if (kerr_failed(copy_from_user(msg, (const void *)user_msg, sizeof(*msg))))
    {
        return -EFAULT;
    }
    if (msg->iovlen > MAX_IOV)
    {
        return -EINVAL;
    }
    uint32_t count = (uint32_t)msg->iovlen;
    if (count > 0 &&
        kerr_failed(copy_from_user(iov, (const void *)msg->iov, count * sizeof(kiovec_t))))
    {
        return -EFAULT;
    }
    size_t total = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        if (iov[i].len > 0x7FFFFFFFu - total)
        {
            return -EINVAL; // the result must fit in the return value
        }
        total += iov[i].len;
        if (!user_access_ok((void *)iov[i].base, iov[i].len, write))
        {
            return -EFAULT;
        }
    }
    return (int32_t)count;
}

/**
 * SYS_sendmsg: send data, and pass open files with SCM_RIGHTS.
 * args: fd, const struct msghdr *, flags (MSG_DONTWAIT, MSG_NOSIGNAL).
 * the files go with the first byte sent, so a message that passes files
 * must have some data. msg_name is ignored (the socket is connected).
 * returns the bytes sent, or -EPIPE (and SIGPIPE, unless MSG_NOSIGNAL),
 * -EAGAIN, -ENOTCONN, -EBADF, -ENOTSOCK, -EINVAL, -EFAULT, -ENOMEM.
 */
syscall_ret_t sys_sendmsg(struct registers *regs)
{
    int fd = (int)syscall_arg(regs, 0);
    uint32_t flags = (uint32_t)syscall_arg(regs, 2);

    kmsghdr_t msg;
    kiovec_t iov[MAX_IOV];
    int32_t iov_count = msghdr_from_user(syscall_arg(regs, 1), &msg, iov, false);
    if (iov_count < 0)
    {
        return iov_count;
    }
    fd_entry_t *entry = fd_get(fd);
    if (!entry)
    {
        return -EBADF;
    }

    // the files to pass, from the control messages
    file_t *files[SOCKET_MAX_FDS];
    uint32_t file_count = 0;
    if (msg.control && msg.controllen > 0)
    {
        if (msg.controllen > MAX_CONTROL)
        {
            return -ENOBUFS;
        }
        uint8_t control[MAX_CONTROL];
        if (kerr_failed(copy_from_user(control, (const void *)msg.control, msg.controllen)))
        {
            return -EFAULT;
        }
        size_t at = 0;
        while (at + sizeof(kcmsghdr_t) <= msg.controllen)
        {
            kcmsghdr_t header;
            memcpy(&header, control + at, sizeof(header));
            if (header.len < sizeof(header) || header.len > msg.controllen - at)
            {
                return -EINVAL;
            }
            if (header.level != SOL_SOCKET || header.type != SCM_RIGHTS)
            {
                return -EINVAL;
            }
            uint32_t n = (uint32_t)((header.len - CMSG_HDR_SIZE) / sizeof(int32_t));
            if (header.len < CMSG_HDR_SIZE || file_count + n > SOCKET_MAX_FDS)
            {
                return -EINVAL;
            }
            for (uint32_t i = 0; i < n; i++)
            {
                int32_t passed;
                memcpy(&passed, control + at + CMSG_HDR_SIZE + i * sizeof(int32_t),
                       sizeof(passed));
                fd_entry_t *passed_entry = fd_get(passed);
                if (!passed_entry)
                {
                    return -EBADF;
                }
                files[file_count++] = passed_entry->file;
            }
            at += CMSG_ALIGN_K(header.len);
        }
    }

    size_t total = 0;
    for (int32_t i = 0; i < iov_count; i++)
    {
        total += iov[i].len;
    }
    if (total == 0)
    {
        // passed files need a byte to travel with
        return file_count > 0 ? -EINVAL : 0;
    }

    int32_t sent = unix_socket_send(entry->file, iov, (uint32_t)iov_count, files, file_count,
                                    (flags & MSG_DONTWAIT) != 0);
    if (sent == -EPIPE && !(flags & MSG_NOSIGNAL))
    {
        signal_send(process_current(), SIGPIPE);
    }
    return sent;
}

/**
 * SYS_recvmsg: receive data, and the files passed with it.
 * args: fd, struct msghdr *, flags (MSG_DONTWAIT, MSG_CMSG_CLOEXEC).
 * the files come as one SCM_RIGHTS control message. files that do not fit
 * in msg_control (or in the fd table) are closed, and msg_flags gets
 * MSG_CTRUNC. msg_namelen becomes 0 (the peer has no name).
 * returns the bytes received (0 at the end of the stream), or -EAGAIN,
 * -ENOTCONN, -EBADF, -ENOTSOCK, -EINVAL, -EFAULT.
 */
syscall_ret_t sys_recvmsg(struct registers *regs)
{
    int fd = (int)syscall_arg(regs, 0);
    uintptr_t user_msg = syscall_arg(regs, 1);
    uint32_t flags = (uint32_t)syscall_arg(regs, 2);

    kmsghdr_t msg;
    kiovec_t iov[MAX_IOV];
    int32_t iov_count = msghdr_from_user(user_msg, &msg, iov, true);
    if (iov_count < 0)
    {
        return iov_count;
    }
    // checked up front: the fds are installed before they are copied out
    if (!user_access_ok((void *)user_msg, sizeof(msg), true) ||
        (msg.control && !user_access_ok((void *)msg.control, msg.controllen, true)))
    {
        return -EFAULT;
    }
    fd_entry_t *entry = fd_get(fd);
    if (!entry)
    {
        return -EBADF;
    }

    file_t *files[SOCKET_MAX_FDS];
    uint32_t file_count = 0;
    int32_t received = unix_socket_receive(entry->file, iov, (uint32_t)iov_count, files,
                                           &file_count, (flags & MSG_DONTWAIT) != 0);
    if (received < 0)
    {
        return received;
    }

    // install as many files as fit in the control buffer
    uint32_t room = 0;
    if (msg.control && msg.controllen >= CMSG_HDR_SIZE)
    {
        room = (uint32_t)((msg.controllen - CMSG_HDR_SIZE) / sizeof(int32_t));
    }
    int32_t fds[SOCKET_MAX_FDS];
    uint32_t installed = 0;
    msg.flags = 0;
    process_t *proc = process_current();
    uint32_t fd_flags = (flags & MSG_CMSG_CLOEXEC) ? FD_CLOEXEC : 0;
    for (uint32_t i = 0; i < file_count; i++)
    {
        int new_fd = installed < room ? fd_install(proc, files[i], fd_flags, 0) : -EMFILE;
        if (new_fd < 0)
        {
            file_unref(files[i]);
            msg.flags |= MSG_CTRUNC;
            continue;
        }
        fds[installed++] = new_fd;
    }

    if (installed > 0)
    {
        uint8_t control[CMSG_HDR_SIZE + SOCKET_MAX_FDS * sizeof(int32_t)];
        kcmsghdr_t header;
        memset(control, 0, sizeof(control));
        header.len = CMSG_HDR_SIZE + installed * sizeof(int32_t);
        header.level = SOL_SOCKET;
        header.type = SCM_RIGHTS;
        memcpy(control, &header, sizeof(header));
        memcpy(control + CMSG_HDR_SIZE, fds, installed * sizeof(int32_t));
        copy_to_user((void *)msg.control, control, header.len);
        msg.controllen = CMSG_ALIGN_K(header.len) <= msg.controllen ? CMSG_ALIGN_K(header.len)
                                                                    : header.len;
    }
    else
    {
        msg.controllen = 0;
    }
    msg.namelen = 0;
    copy_to_user((void *)user_msg, &msg, sizeof(msg));
    return received;
}

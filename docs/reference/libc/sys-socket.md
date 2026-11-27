<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/socket.h&gt;

Source: [`src/libc/include/sys/socket.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/socket.h)

sockets: Unix domain stream sockets, with file descriptor
       passing, and IPv4 sockets (TCP; UDP, and ICMP for ping).

AF\_UNIX with SOCK\_STREAM, and AF\_INET with SOCK\_STREAM or SOCK\_DGRAM
(&lt;netinet/in.h&gt; has the addresses). the values and layouts are the same as Linux's,
and must match src/kernel/ipc/unix\_socket.h and
src/kernel/net/inet\_socket.h.

to pass open files, put their fds in an SCM\_RIGHTS control message of
sendmsg; the message must have at least one byte of data. the receiver
gets new fds for the same open files from recvmsg.

## \_\_cmsg\_nxthdr

```c
struct cmsghdr *__cmsg_nxthdr(struct msghdr *msg, struct cmsghdr *cmsg);
```

the control message after cmsg, or NULL. use CMSG\_NXTHDR.

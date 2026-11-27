<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;netdb.h&gt;

Source: [`src/libc/include/netdb.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/netdb.h)

names of hosts and services: getaddrinfo and the older calls.

a host name is looked up in /etc/hosts first, then with DNS (the name
servers in /etc/resolv.conf, which dhcp writes). only IPv4 addresses (A
records). the service names are a built-in table of the common ones.
the values are Linux's.

## getaddrinfo

```c
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **res);
```

the addresses for a host and a service.

- `node`: a host name or address; NULL for the local host, or with AI\_PASSIVE the address that takes any.
- `service`: a port number or name ("http"), or NULL for port 0.
- `hints`: the family (AF\_UNSPEC or AF\_INET), the socket type (0 for both SOCK\_STREAM and SOCK\_DGRAM) and the AI\_ flags; may be NULL.
- `res`: receives a list; free it with freeaddrinfo.

Returns: 0, or an EAI\_ error.

## gai\_strerror

```c
const char *gai_strerror(int error);
```

the text of an EAI\_ error.

## getnameinfo

```c
int getnameinfo(const struct sockaddr *addr, socklen_t addrlen, char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);
```

the host and service of an address: the name from /etc/hosts, else the
address as text (there are no reverse DNS lookups); the service name
from the table, else the number.

Returns: 0, or an EAI\_ error (EAI\_NONAME with NI\_NAMEREQD and no name).

## gethostbyname

```c
struct hostent *gethostbyname(const char *name);
```

the older lookup: the IPv4 addresses of a name, in a buffer of the
calling thread. sets h\_errno on an error.

Returns: the entry, or NULL.

## getservbyname, getservbyport

```c
struct servent *getservbyname(const char *name, const char *proto);
struct servent *getservbyport(int port, const char *proto);
```

a service by name or by port (network byte order); proto "tcp", "udp"
or NULL. in a buffer of the calling thread.

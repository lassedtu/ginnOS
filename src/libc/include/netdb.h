#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

/**
 * @file netdb.h
 * @brief names of hosts and services: getaddrinfo and the older calls.
 *
 * a host name is looked up in /etc/hosts first, then with DNS (the name
 * servers in /etc/resolv.conf, which dhcp writes). only IPv4 addresses (A
 * records). the service names are a built-in table of the common ones.
 * the values are Linux's.
 */

struct addrinfo
{
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

// ai_flags
#define AI_PASSIVE     0x0001 // no node: the address to bind to (any)
#define AI_CANONNAME   0x0002 // the first result gets the canonical name
#define AI_NUMERICHOST 0x0004 // node must be an address; no lookup
#define AI_V4MAPPED    0x0008
#define AI_ALL         0x0010
#define AI_ADDRCONFIG  0x0020
#define AI_NUMERICSERV 0x0400 // service must be a number

// the errors of getaddrinfo and getnameinfo
#define EAI_BADFLAGS -1
#define EAI_NONAME   -2 // no such name
#define EAI_AGAIN    -3 // no answer now; it may work later
#define EAI_FAIL     -4
#define EAI_FAMILY   -6
#define EAI_SOCKTYPE -7
#define EAI_SERVICE  -8
#define EAI_MEMORY   -10
#define EAI_SYSTEM   -11 // see errno
#define EAI_OVERFLOW -12

// getnameinfo
#define NI_MAXHOST     1025
#define NI_MAXSERV     32
#define NI_NUMERICHOST 0x01
#define NI_NUMERICSERV 0x02
#define NI_NOFQDN      0x04
#define NI_NAMEREQD    0x08
#define NI_DGRAM       0x10

/**
 * the addresses for a host and a service.
 * @param node a host name or address; NULL for the local host, or with
 *        AI_PASSIVE the address that takes any.
 * @param service a port number or name ("http"), or NULL for port 0.
 * @param hints the family (AF_UNSPEC or AF_INET), the socket type (0 for
 *        both SOCK_STREAM and SOCK_DGRAM) and the AI_ flags; may be NULL.
 * @param res receives a list; free it with freeaddrinfo.
 * @return 0, or an EAI_ error.
 */
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **res);

void freeaddrinfo(struct addrinfo *res);

/**
 * the text of an EAI_ error.
 */
const char *gai_strerror(int error);

/**
 * the host and service of an address: the name from /etc/hosts, else the
 * address as text (there are no reverse DNS lookups); the service name
 * from the table, else the number.
 * @return 0, or an EAI_ error (EAI_NONAME with NI_NAMEREQD and no name).
 */
int getnameinfo(const struct sockaddr *addr, socklen_t addrlen, char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);

struct hostent
{
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};
#define h_addr h_addr_list[0]

struct servent
{
    char *s_name;
    char **s_aliases;
    int s_port; // network byte order
    char *s_proto;
};

// h_errno
#define HOST_NOT_FOUND 1
#define TRY_AGAIN      2
#define NO_RECOVERY    3
#define NO_DATA        4

int *__h_errno_location(void);
#define h_errno (*__h_errno_location())

/**
 * the older lookup: the IPv4 addresses of a name, in a buffer of the
 * calling thread. sets h_errno on an error.
 * @return the entry, or NULL.
 */
struct hostent *gethostbyname(const char *name);

/**
 * a service by name or by port (network byte order); proto "tcp", "udp"
 * or NULL. in a buffer of the calling thread.
 */
struct servent *getservbyname(const char *name, const char *proto);
struct servent *getservbyport(int port, const char *proto);

const char *hstrerror(int error);

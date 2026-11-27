/**
 * @file netdb.c
 * @brief host and service names (netdb.h): getaddrinfo and the others.
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dns.h"

typedef struct
{
    const char *name;
    uint16_t port;
} service_t;

// the common services (as /etc/services); each is TCP and UDP
static const service_t services[] = {
    {"ftp", 21},     {"ssh", 22},   {"telnet", 23}, {"smtp", 25},     {"domain", 53},
    {"http", 80},    {"pop3", 110}, {"ntp", 123},   {"imap", 143},    {"https", 443},
    {"imaps", 993},  {"pop3s", 995}, {"irc", 6667}, {"ircs", 6697},   {"bootps", 67},
    {"bootpc", 68},  {"submission", 587},
};
#define SERVICE_COUNT (sizeof(services) / sizeof(services[0]))

static __thread int thread_h_errno;

int *__h_errno_location(void)
{
    return &thread_h_errno;
}

/**
 * a port from a number or a name, in host byte order.
 * @return 0, or EAI_SERVICE (EAI_NONAME with AI_NUMERICSERV).
 */
static int service_port(const char *service, int flags, uint16_t *port)
{
    *port = 0;
    if (!service)
    {
        return 0;
    }
    char *end;
    unsigned long n = strtoul(service, &end, 10);
    if (*service != '\0' && *end == '\0')
    {
        if (n > 65535)
        {
            return EAI_SERVICE;
        }
        *port = (uint16_t)n;
        return 0;
    }
    if (flags & AI_NUMERICSERV)
    {
        return EAI_NONAME;
    }
    for (size_t i = 0; i < SERVICE_COUNT; i++)
    {
        if (strcmp(services[i].name, service) == 0)
        {
            *port = services[i].port;
            return 0;
        }
    }
    return EAI_SERVICE;
}

/**
 * the addresses of a name in /etc/hosts.
 * @param reverse look up the name of *addresses instead (into name_out).
 * @return the number found.
 */
static int hosts_file(const char *name, uint32_t *addresses, int max, bool reverse,
                      char *name_out, size_t name_size)
{
    FILE *f = fopen("/etc/hosts", "r");
    if (!f)
    {
        return 0;
    }
    int count = 0;
    char line[512];
    while (count < max && fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "#\n")] = '\0';
        char *save;
        char *address = strtok_r(line, " \t", &save);
        struct in_addr a;
        if (!address || inet_pton(AF_INET, address, &a) != 1)
        {
            continue;
        }
        for (char *host = strtok_r(NULL, " \t", &save); host; host = strtok_r(NULL, " \t", &save))
        {
            if (reverse && a.s_addr == addresses[0])
            {
                snprintf(name_out, name_size, "%s", host);
                fclose(f);
                return 1;
            }
            if (!reverse && strcasecmp(host, name) == 0)
            {
                addresses[count++] = a.s_addr;
                break;
            }
        }
    }
    fclose(f);
    return reverse ? 0 : count;
}

/**
 * the addresses of a node.
 * @return the number of addresses, or an EAI_ error.
 */
static int resolve(const char *node, int flags, uint32_t *addresses, char *canonical)
{
    if (!node)
    {
        addresses[0] = (flags & AI_PASSIVE) ? INADDR_ANY : htonl(INADDR_LOOPBACK);
        snprintf(canonical, DNS_NAME_MAX, "%s", "localhost");
        return 1;
    }
    snprintf(canonical, DNS_NAME_MAX, "%s", node);
    struct in_addr a;
    if (inet_aton(node, &a))
    {
        addresses[0] = a.s_addr;
        return 1;
    }
    if (flags & AI_NUMERICHOST)
    {
        return EAI_NONAME;
    }
    int count = hosts_file(node, addresses, DNS_MAX_ADDRESSES, false, NULL, 0);
    if (count > 0)
    {
        return count;
    }
    // the machine's own name, when /etc/hosts does not list it
    char host[256];
    if (strcasecmp(node, "localhost") == 0 ||
        (gethostname(host, sizeof(host)) == 0 && strcasecmp(node, host) == 0))
    {
        addresses[0] = htonl(INADDR_LOOPBACK);
        return 1;
    }
    // a name that ends with a dot is already complete
    char name[DNS_NAME_MAX];
    snprintf(name, sizeof(name), "%s", node);
    size_t length = strlen(name);
    if (length > 0 && name[length - 1] == '.')
    {
        name[length - 1] = '\0';
    }
    return __dns_lookup(name, addresses, DNS_MAX_ADDRESSES, canonical);
}

static struct addrinfo *make_entry(uint32_t address, uint16_t port, int socktype,
                                   const char *canonical)
{
    size_t canon_size = canonical ? strlen(canonical) + 1 : 0;
    struct addrinfo *ai = calloc(1, sizeof(*ai) + sizeof(struct sockaddr_in) + canon_size);
    if (!ai)
    {
        return NULL;
    }
    struct sockaddr_in *sin = (struct sockaddr_in *)(ai + 1);
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    sin->sin_addr.s_addr = address;
    ai->ai_family = AF_INET;
    ai->ai_socktype = socktype;
    ai->ai_protocol = socktype == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    ai->ai_addrlen = sizeof(*sin);
    ai->ai_addr = (struct sockaddr *)sin;
    if (canonical)
    {
        ai->ai_canonname = (char *)(sin + 1);
        memcpy(ai->ai_canonname, canonical, canon_size);
    }
    return ai;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **res)
{
    int flags = hints ? hints->ai_flags : 0;
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int socktype = hints ? hints->ai_socktype : 0;
    *res = NULL;
    if (!node && !service)
    {
        return EAI_NONAME;
    }
    if (family != AF_UNSPEC && family != AF_INET)
    {
        return EAI_FAMILY;
    }
    if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
    {
        return EAI_SOCKTYPE;
    }
    uint16_t port;
    int err = service_port(service, flags, &port);
    if (err != 0)
    {
        return err;
    }
    uint32_t addresses[DNS_MAX_ADDRESSES];
    char canonical[DNS_NAME_MAX];
    int count = resolve(node, flags, addresses, canonical);
    if (count < 0)
    {
        return count;
    }

    // each address, for each socket type asked for
    static const int both[] = {SOCK_STREAM, SOCK_DGRAM};
    int types = socktype ? 1 : 2;
    struct addrinfo **tail = res;
    for (int i = 0; i < count; i++)
    {
        for (int t = 0; t < types; t++)
        {
            bool first = tail == res;
            struct addrinfo *ai = make_entry(addresses[i], port, socktype ? socktype : both[t],
                                             first && (flags & AI_CANONNAME) ? canonical : NULL);
            if (!ai)
            {
                freeaddrinfo(*res);
                *res = NULL;
                return EAI_MEMORY;
            }
            *tail = ai;
            tail = &ai->ai_next;
        }
    }
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    while (res)
    {
        struct addrinfo *next = res->ai_next;
        free(res);
        res = next;
    }
}

const char *gai_strerror(int error)
{
    switch (error)
    {
    case 0:            return "success";
    case EAI_BADFLAGS: return "bad value for ai_flags";
    case EAI_NONAME:   return "name or service not known";
    case EAI_AGAIN:    return "temporary failure in name resolution";
    case EAI_FAIL:     return "non-recoverable failure in name resolution";
    case EAI_FAMILY:   return "address family not supported";
    case EAI_SOCKTYPE: return "socket type not supported";
    case EAI_SERVICE:  return "service not supported for socket type";
    case EAI_MEMORY:   return "out of memory";
    case EAI_SYSTEM:   return strerror(errno);
    case EAI_OVERFLOW: return "argument buffer overflow";
    }
    return "unknown error";
}

int getnameinfo(const struct sockaddr *addr, socklen_t addrlen, char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags)
{
    if (!addr || addrlen < sizeof(struct sockaddr_in) || addr->sa_family != AF_INET)
    {
        return EAI_FAMILY;
    }
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    if (host && hostlen > 0)
    {
        uint32_t address = sin->sin_addr.s_addr;
        char name[NI_MAXHOST];
        bool named = !(flags & NI_NUMERICHOST) &&
                     hosts_file(NULL, &address, 1, true, name, sizeof(name)) == 1;
        if (!named)
        {
            if (flags & NI_NAMEREQD)
            {
                return EAI_NONAME;
            }
            inet_ntop(AF_INET, &sin->sin_addr, name, sizeof(name));
        }
        if (strlen(name) >= hostlen)
        {
            return EAI_OVERFLOW;
        }
        strcpy(host, name);
    }
    if (serv && servlen > 0)
    {
        uint16_t port = ntohs(sin->sin_port);
        char text[NI_MAXSERV];
        snprintf(text, sizeof(text), "%u", port);
        for (size_t i = 0; i < SERVICE_COUNT && !(flags & NI_NUMERICSERV); i++)
        {
            if (services[i].port == port)
            {
                snprintf(text, sizeof(text), "%s", services[i].name);
                break;
            }
        }
        if (strlen(text) >= servlen)
        {
            return EAI_OVERFLOW;
        }
        strcpy(serv, text);
    }
    return 0;
}

struct hostent *gethostbyname(const char *name)
{
    static __thread struct hostent entry;
    static __thread char canonical[DNS_NAME_MAX];
    static __thread uint32_t addresses[DNS_MAX_ADDRESSES];
    static __thread char *list[DNS_MAX_ADDRESSES + 1];
    static __thread char *no_aliases[1];

    int count = resolve(name, 0, addresses, canonical);
    if (count < 0)
    {
        h_errno = count == EAI_NONAME ? HOST_NOT_FOUND : count == EAI_AGAIN ? TRY_AGAIN : NO_RECOVERY;
        return NULL;
    }
    for (int i = 0; i < count; i++)
    {
        list[i] = (char *)&addresses[i];
    }
    list[count] = NULL;
    no_aliases[0] = NULL;
    entry.h_name = canonical;
    entry.h_aliases = no_aliases;
    entry.h_addrtype = AF_INET;
    entry.h_length = 4;
    entry.h_addr_list = list;
    return &entry;
}

static struct servent *make_servent(const service_t *s, const char *proto)
{
    static __thread struct servent entry;
    static __thread char *no_aliases[1];
    no_aliases[0] = NULL;
    entry.s_name = (char *)s->name;
    entry.s_aliases = no_aliases;
    entry.s_port = htons(s->port);
    entry.s_proto = (char *)(proto ? proto : "tcp");
    return &entry;
}

struct servent *getservbyname(const char *name, const char *proto)
{
    for (size_t i = 0; i < SERVICE_COUNT; i++)
    {
        if (strcmp(services[i].name, name) == 0)
        {
            return make_servent(&services[i], proto);
        }
    }
    return NULL;
}

struct servent *getservbyport(int port, const char *proto)
{
    for (size_t i = 0; i < SERVICE_COUNT; i++)
    {
        if (htons(services[i].port) == port)
        {
            return make_servent(&services[i], proto);
        }
    }
    return NULL;
}

const char *hstrerror(int error)
{
    switch (error)
    {
    case HOST_NOT_FOUND: return "unknown host";
    case TRY_AGAIN:      return "host name lookup failure";
    case NO_RECOVERY:    return "unknown server error";
    case NO_DATA:        return "no address associated with name";
    }
    return "unknown resolver error";
}

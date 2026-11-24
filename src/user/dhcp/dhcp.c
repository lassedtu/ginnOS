/**
 * @file dhcp.c
 * @brief dhcp: get an IPv4 address for a network interface (RFC 2131).
 *
 * the client sends DISCOVER to all hosts, takes the first OFFER, asks
 * for it with REQUEST, and on ACK sets the interface's address, netmask
 * and gateway, and writes the name servers to /etc/resolv.conf.
 *
 * with -b it stays: it renews the lease at half its time (T1), and when
 * the lease ends without a renewal it takes the address away and starts
 * again. init starts "dhcp -b <if>" for each network card.
 *
 * usage: dhcp [-b] [interface]   (the first Ethernet interface if none)
 */

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SERVER_PORT 67
#define CLIENT_PORT 68

#define FIXED_SIZE   236 // the message up to the options
#define MAGIC        0x63825363
#define MESSAGE_SIZE 576 // the least a server must take

// message types (option 53)
#define DISCOVER 1
#define OFFER    2
#define REQUEST  3
#define ACK      5
#define NAK      6

// options
#define OPT_PAD       0
#define OPT_NETMASK   1
#define OPT_ROUTER    3
#define OPT_DNS       6
#define OPT_HOSTNAME  12
#define OPT_REQUESTED 50
#define OPT_LEASE     51
#define OPT_TYPE      53
#define OPT_SERVER    54
#define OPT_PARAMS    55
#define OPT_END       255

#define TRIES        5
#define MAX_DNS      3
#define RETRY_LATER  60 // seconds between renewals that got no answer

typedef struct
{
    uint8_t type;
    in_addr_t address; // yiaddr
    in_addr_t server;
    in_addr_t netmask;
    in_addr_t router;
    in_addr_t dns[MAX_DNS];
    int dns_count;
    uint32_t lease; // seconds
} reply_t;

static int sock;
static const char *ifname;
static unsigned char mac[6];
static uint32_t xid;
static bool quiet_errors;

static void say(const char *format, const char *detail)
{
    fprintf(stderr, "dhcp: %s: ", ifname);
    fprintf(stderr, format, detail);
    fprintf(stderr, "\n");
}

static bool first_ethernet(char *name, size_t size)
{
    FILE *f = fopen("/proc/net", "r");
    if (!f)
    {
        return false;
    }
    char line[256];
    bool found = false;
    while (!found && fgets(line, sizeof(line), f))
    {
        size_t length = strcspn(line, "\t ");
        if (length > 0 && length < size && strncmp(line, "lo", length) != 0)
        {
            memcpy(name, line, length);
            name[length] = '\0';
            found = true;
        }
    }
    fclose(f);
    return found;
}

static int set_address(unsigned long what, in_addr_t value)
{
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strncpy(r.ifr_name, ifname, IFNAMSIZ - 1);
    struct sockaddr_in *a = (struct sockaddr_in *)&r.ifr_addr;
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = value;
    return ioctl(sock, what, &r);
}

static uint8_t *put_option(uint8_t *p, uint8_t code, const void *data, uint8_t length)
{
    *p++ = code;
    *p++ = length;
    memcpy(p, data, length);
    return p + length;
}

/**
 * send a DISCOVER or a REQUEST to all hosts.
 * @param current the address the client has (a renewal), or 0.
 */
static bool send_message(uint8_t type, in_addr_t requested, in_addr_t server, in_addr_t current)
{
    uint8_t m[MESSAGE_SIZE];
    memset(m, 0, sizeof(m));
    m[0] = 1; // a request from a client
    m[1] = 1; // Ethernet
    m[2] = 6;
    memcpy(m + 4, &xid, 4);
    if (current == 0)
    {
        m[10] = 0x80; // flags: answer to all hosts (the client has no address yet)
    }
    memcpy(m + 12, &current, 4); // ciaddr
    memcpy(m + 28, mac, 6);      // chaddr
    uint32_t magic = htonl(MAGIC);
    memcpy(m + FIXED_SIZE, &magic, 4);

    uint8_t *p = m + FIXED_SIZE + 4;
    p = put_option(p, OPT_TYPE, &type, 1);
    if (requested != 0 && current == 0)
    {
        p = put_option(p, OPT_REQUESTED, &requested, 4);
    }
    if (server != 0 && current == 0)
    {
        p = put_option(p, OPT_SERVER, &server, 4);
    }
    static const uint8_t params[] = {OPT_NETMASK, OPT_ROUTER, OPT_DNS, OPT_LEASE};
    p = put_option(p, OPT_PARAMS, params, sizeof(params));
    char host[64];
    if (gethostname(host, sizeof(host)) == 0 && host[0] != '\0')
    {
        p = put_option(p, OPT_HOSTNAME, host, (uint8_t)strnlen(host, 63));
    }
    *p++ = OPT_END;

    struct sockaddr_in to = {0};
    to.sin_family = AF_INET;
    to.sin_port = htons(SERVER_PORT);
    to.sin_addr.s_addr = INADDR_BROADCAST;
    if (sendto(sock, m, sizeof(m), 0, (struct sockaddr *)&to, sizeof(to)) < 0)
    {
        if (!quiet_errors)
        {
            say("cannot send: %s", strerror(errno));
        }
        return false;
    }
    return true;
}

/**
 * read the options of a reply.
 * @return false if the reply is not for this client or is malformed.
 */
static bool parse(const uint8_t *m, size_t length, reply_t *r)
{
    memset(r, 0, sizeof(*r));
    uint32_t magic;
    if (length < FIXED_SIZE + 4 || m[0] != 2 || memcmp(m + 4, &xid, 4) != 0 ||
        memcmp(m + 28, mac, 6) != 0)
    {
        return false;
    }
    memcpy(&magic, m + FIXED_SIZE, 4);
    if (ntohl(magic) != MAGIC)
    {
        return false;
    }
    memcpy(&r->address, m + 16, 4);
    const uint8_t *p = m + FIXED_SIZE + 4;
    const uint8_t *end = m + length;
    while (p < end && *p != OPT_END)
    {
        if (*p == OPT_PAD)
        {
            p++;
            continue;
        }
        if (p + 2 > end || p + 2 + p[1] > end)
        {
            return false;
        }
        uint8_t code = p[0];
        uint8_t size = p[1];
        const uint8_t *v = p + 2;
        if (code == OPT_TYPE && size >= 1)
        {
            r->type = v[0];
        }
        else if (code == OPT_SERVER && size >= 4)
        {
            memcpy(&r->server, v, 4);
        }
        else if (code == OPT_NETMASK && size >= 4)
        {
            memcpy(&r->netmask, v, 4);
        }
        else if (code == OPT_ROUTER && size >= 4)
        {
            memcpy(&r->router, v, 4); // the first router
        }
        else if (code == OPT_DNS)
        {
            for (int i = 0; i + 4 <= size && r->dns_count < MAX_DNS; i += 4)
            {
                memcpy(&r->dns[r->dns_count++], v + i, 4);
            }
        }
        else if (code == OPT_LEASE && size >= 4)
        {
            uint32_t lease;
            memcpy(&lease, v, 4);
            r->lease = ntohl(lease);
        }
        p += 2 + size;
    }
    return r->type != 0;
}

/**
 * wait for a reply of one of two types.
 * @return false when the time is over.
 */
static bool wait_reply(int seconds, uint8_t want, uint8_t or_this, reply_t *r)
{
    time_t end = time(NULL) + seconds;
    for (;;)
    {
        int left = (int)(end - time(NULL));
        if (left <= 0)
        {
            return false;
        }
        struct pollfd p = {sock, POLLIN, 0};
        if (poll(&p, 1, left * 1000) <= 0)
        {
            continue;
        }
        uint8_t m[1500];
        ssize_t n = recv(sock, m, sizeof(m), 0);
        if (n > 0 && parse(m, (size_t)n, r) && (r->type == want || r->type == or_this))
        {
            return true;
        }
    }
}

static void apply(const reply_t *r)
{
    in_addr_t netmask = r->netmask ? r->netmask : htonl(0xFFFFFF00);
    if (set_address(SIOCSIFADDR, r->address) < 0 || set_address(SIOCSIFNETMASK, netmask) < 0 ||
        set_address(SIOCSIFGATEWAY, r->router) < 0)
    {
        say("cannot set the address: %s", strerror(errno));
        return;
    }
    if (r->dns_count > 0)
    {
        FILE *f = fopen("/etc/resolv.conf", "w");
        if (f)
        {
            fprintf(f, "# from dhcp (%s)\n", ifname);
            for (int i = 0; i < r->dns_count; i++)
            {
                struct in_addr a = {r->dns[i]};
                fprintf(f, "nameserver %s\n", inet_ntoa(a));
            }
            fclose(f);
        }
    }

    char address[INET_ADDRSTRLEN];
    char gateway[INET_ADDRSTRLEN];
    struct in_addr a = {r->address};
    struct in_addr g = {r->router};
    inet_ntop(AF_INET, &a, address, sizeof(address));
    inet_ntop(AF_INET, &g, gateway, sizeof(gateway));
    fprintf(stderr, "dhcp: %s: %s/%d, gateway %s, lease %u s\n", ifname, address,
            __builtin_popcount(netmask), r->router ? gateway : "none", r->lease);
}

/**
 * DISCOVER, OFFER, REQUEST, ACK.
 * @return true with the lease in r.
 */
static bool get_lease(reply_t *r)
{
    for (int attempt = 0; attempt < TRIES; attempt++)
    {
        int wait = 2 << (attempt < 3 ? attempt : 3); // 2, 4, 8, 16, 16 seconds
        xid = (uint32_t)time(NULL) ^ ((uint32_t)getpid() << 16) ^ (uint32_t)attempt * 0x9E3779B9u;
        if (!send_message(DISCOVER, 0, 0, 0))
        {
            sleep(1);
            continue;
        }
        reply_t offer;
        if (!wait_reply(wait, OFFER, OFFER, &offer))
        {
            continue;
        }
        if (!send_message(REQUEST, offer.address, offer.server, 0))
        {
            continue;
        }
        if (wait_reply(wait, ACK, NAK, r) && r->type == ACK)
        {
            if (r->server == 0)
            {
                r->server = offer.server;
            }
            return true;
        }
    }
    return false;
}

/**
 * keep the lease: REQUEST at half of it; start again when it ends.
 */
static void keep(reply_t lease)
{
    for (;;)
    {
        time_t got = time(NULL);
        uint32_t length = lease.lease ? lease.lease : 3600;
        time_t ends = got + (time_t)length;
        sleep(length / 2);

        bool renewed = false;
        while (!renewed && time(NULL) < ends)
        {
            reply_t r;
            quiet_errors = true;
            if (send_message(REQUEST, 0, 0, lease.address) && wait_reply(10, ACK, NAK, &r))
            {
                if (r.type == NAK)
                {
                    break;
                }
                if (r.server == 0)
                {
                    r.server = lease.server;
                }
                lease = r;
                renewed = true;
            }
            else
            {
                sleep(RETRY_LATER);
            }
        }
        quiet_errors = false;
        if (renewed)
        {
            continue;
        }
        // the lease is over: no address until there is a new one
        say("the lease of %s ended", inet_ntoa((struct in_addr){lease.address}));
        set_address(SIOCSIFGATEWAY, 0);
        set_address(SIOCSIFADDR, 0);
        while (!get_lease(&lease))
        {
            sleep(RETRY_LATER);
        }
        apply(&lease);
    }
}

int main(int argc, char **argv)
{
    bool background = false;
    int opt;
    while ((opt = getopt(argc, argv, "b")) != -1)
    {
        if (opt == 'b')
        {
            background = true;
        }
        else
        {
            fprintf(stderr, "usage: dhcp [-b] [interface]\n");
            return 2;
        }
    }
    char name[IFNAMSIZ];
    if (optind < argc)
    {
        ifname = argv[optind];
    }
    else if (first_ethernet(name, sizeof(name)))
    {
        ifname = name;
    }
    else
    {
        fprintf(stderr, "dhcp: there is no network interface\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    int on = 1;
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(CLIENT_PORT);
    if (sock < 0 || setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) < 0 ||
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0 ||
        bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0)
    {
        perror("dhcp: socket");
        return 1;
    }
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strncpy(r.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFHWADDR, &r) < 0)
    {
        say("%s", strerror(errno));
        return 1;
    }
    memcpy(mac, r.ifr_hwaddr.sa_data, 6);

    reply_t lease;
    while (!get_lease(&lease))
    {
        if (!background)
        {
            say("%s", "no answer from a DHCP server");
            return 1;
        }
        sleep(RETRY_LATER);
    }
    apply(&lease);
    if (background)
    {
        keep(lease);
    }
    return 0;
}

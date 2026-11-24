/**
 * @file ip.c
 * @brief ip: show and set the network interfaces.
 *
 * usage:
 *   ip [addr]                          show the interfaces
 *   ip addr set <if> <address>/<bits>  set the address and the netmask
 *   ip route set default via <address> [dev <if>]
 *                                      set the gateway (of the first
 *                                      Ethernet interface, or of <if>)
 *
 * the names come from /proc/net; the rest from the SIOC ioctls on a
 * socket. setting needs root.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_INTERFACES 8

static int sock;

static int interface_names(char names[MAX_INTERFACES][IFNAMSIZ])
{
    FILE *f = fopen("/proc/net", "r");
    if (!f)
    {
        return 0;
    }
    int count = 0;
    char line[256];
    while (count < MAX_INTERFACES && fgets(line, sizeof(line), f))
    {
        size_t length = strcspn(line, "\t ");
        if (length > 0 && length < IFNAMSIZ)
        {
            memcpy(names[count], line, length);
            names[count][length] = '\0';
            count++;
        }
    }
    fclose(f);
    return count;
}

static int request(const char *name, unsigned long what, struct ifreq *r)
{
    memset(r, 0, sizeof(*r));
    strncpy(r->ifr_name, name, IFNAMSIZ - 1);
    return ioctl(sock, what, r);
}

static in_addr_t address_of(const char *name, unsigned long what)
{
    struct ifreq r;
    if (request(name, what, &r) < 0)
    {
        return 0;
    }
    return ((struct sockaddr_in *)&r.ifr_addr)->sin_addr.s_addr;
}

static int prefix_bits(in_addr_t mask)
{
    return __builtin_popcount(mask);
}

static void show(const char *name)
{
    struct ifreq r;
    short flags = request(name, SIOCGIFFLAGS, &r) == 0 ? r.ifr_flags : 0;
    int mtu = request(name, SIOCGIFMTU, &r) == 0 ? r.ifr_mtu : 0;
    printf("%s: %s, mtu %d\n", name, (flags & IFF_UP) ? "up" : "down", mtu);
    if (!(flags & IFF_LOOPBACK) && request(name, SIOCGIFHWADDR, &r) == 0)
    {
        const unsigned char *m = (const unsigned char *)r.ifr_hwaddr.sa_data;
        printf("    ether %02x:%02x:%02x:%02x:%02x:%02x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    }
    struct in_addr address = {address_of(name, SIOCGIFADDR)};
    struct in_addr gateway = {address_of(name, SIOCGIFGATEWAY)};
    if (address.s_addr == 0)
    {
        printf("    no address\n");
        return;
    }
    printf("    inet %s/%d", inet_ntoa(address), prefix_bits(address_of(name, SIOCGIFNETMASK)));
    if (gateway.s_addr != 0)
    {
        printf("  gateway %s", inet_ntoa(gateway));
    }
    printf("\n");
}

static int set_address(const char *name, unsigned long what, in_addr_t value)
{
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strncpy(r.ifr_name, name, IFNAMSIZ - 1);
    struct sockaddr_in *a = (struct sockaddr_in *)&r.ifr_addr;
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = value;
    if (ioctl(sock, what, &r) < 0)
    {
        fprintf(stderr, "ip: %s: %s\n", name, strerror(errno));
        return 1;
    }
    return 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: ip [addr]\n"
                    "       ip addr set <interface> <address>/<bits>\n"
                    "       ip route set default via <address> [dev <interface>]\n");
    return 2;
}

int main(int argc, char **argv)
{
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("ip: socket");
        return 1;
    }
    char names[MAX_INTERFACES][IFNAMSIZ];
    int count = interface_names(names);

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "addr") == 0))
    {
        for (int i = 0; i < count; i++)
        {
            show(names[i]);
        }
        return 0;
    }

    if (argc == 5 && strcmp(argv[1], "addr") == 0 && strcmp(argv[2], "set") == 0)
    {
        char text[32];
        snprintf(text, sizeof(text), "%s", argv[4]);
        char *slash = strchr(text, '/');
        int bits = slash ? atoi(slash + 1) : 24;
        if (slash)
        {
            *slash = '\0';
        }
        struct in_addr address;
        if (inet_pton(AF_INET, text, &address) != 1 || bits < 1 || bits > 32)
        {
            fprintf(stderr, "ip: %s: not an address with a prefix (10.0.2.15/24)\n", argv[4]);
            return 2;
        }
        in_addr_t mask = htonl(bits == 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> bits));
        return set_address(argv[3], SIOCSIFADDR, address.s_addr) ||
               set_address(argv[3], SIOCSIFNETMASK, mask);
    }

    if ((argc == 6 || argc == 8) && strcmp(argv[1], "route") == 0 &&
        strcmp(argv[2], "set") == 0 && strcmp(argv[3], "default") == 0 &&
        strcmp(argv[4], "via") == 0)
    {
        struct in_addr gateway;
        if (inet_pton(AF_INET, argv[5], &gateway) != 1)
        {
            fprintf(stderr, "ip: %s: not an IPv4 address\n", argv[5]);
            return 2;
        }
        const char *name = NULL;
        if (argc == 8)
        {
            if (strcmp(argv[6], "dev") != 0)
            {
                return usage();
            }
            name = argv[7];
        }
        for (int i = 0; i < count && !name; i++)
        {
            if (strcmp(names[i], "lo") != 0)
            {
                name = names[i];
            }
        }
        if (!name)
        {
            fprintf(stderr, "ip: there is no network interface\n");
            return 1;
        }
        return set_address(name, SIOCSIFGATEWAY, gateway.s_addr);
    }
    return usage();
}

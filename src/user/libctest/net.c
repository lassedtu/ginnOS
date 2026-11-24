/**
 * @file net.c
 * @brief libctest: IPv4 addresses as text, and UDP and ping over the
 *        loopback interface.
 */

#include "libctest.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_addresses(void)
{
    struct in_addr a;
    CHECK(inet_pton(AF_INET, "10.0.2.15", &a) == 1 && a.s_addr == htonl(0x0A00020F));
    CHECK(inet_pton(AF_INET, "256.0.0.1", &a) == 0);
    CHECK(inet_pton(AF_INET, "1.2.3", &a) == 0);
    CHECK(inet_pton(AF_INET, "01.2.3.4", &a) == 0);
    CHECK(inet_pton(AF_INET, "1.2.3.4 ", &a) == 0);
    char text[INET_ADDRSTRLEN];
    a.s_addr = htonl(0xC0A80101);
    CHECK(inet_ntop(AF_INET, &a, text, sizeof(text)) == text && strcmp(text, "192.168.1.1") == 0);
    CHECK(inet_ntop(AF_INET, &a, text, 8) == NULL && errno == ENOSPC);
    CHECK(strcmp(inet_ntoa(a), "192.168.1.1") == 0);
    CHECK(inet_addr("127.1") == htonl(0x7F000001));
    CHECK(inet_addr("0x7f.0.0.1") == htonl(0x7F000001));
    CHECK(inet_addr("1.2.3.4.5") == INADDR_NONE);
    CHECK(htons(0x1234) == 0x3412 && ntohl(htonl(0xDEADBEEF)) == 0xDEADBEEF);
}

static void test_udp(void)
{
    int server = socket(AF_INET, SOCK_DGRAM, 0);
    int client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(server >= 0 && client >= 0);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // any free port
    CHECK(bind(server, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    socklen_t length = sizeof(addr);
    CHECK(getsockname(server, (struct sockaddr *)&addr, &length) == 0);
    CHECK(length == sizeof(addr) && ntohs(addr.sin_port) >= 49152);

    // a second bind of the same port is refused
    int other = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(bind(other, (struct sockaddr *)&addr, sizeof(addr)) == -1 && errno == EADDRINUSE);
    close(other);

    CHECK(sendto(client, "hello", 5, 0, (struct sockaddr *)&addr, sizeof(addr)) == 5);
    struct pollfd p = {server, POLLIN, 0};
    CHECK(poll(&p, 1, 1000) == 1 && (p.revents & POLLIN));

    char buffer[16];
    struct sockaddr_in from;
    socklen_t from_length = sizeof(from);
    CHECK(recvfrom(server, buffer, 3, MSG_PEEK, (struct sockaddr *)&from, &from_length) == 3);
    CHECK(recvfrom(server, buffer, sizeof(buffer), 0, (struct sockaddr *)&from, &from_length) ==
          5);
    CHECK(memcmp(buffer, "hello", 5) == 0);
    CHECK(from.sin_family == AF_INET && from.sin_addr.s_addr == htonl(INADDR_LOOPBACK));

    // the answer goes back to the client's port; a short buffer cuts it
    CHECK(sendto(server, "world!", 6, 0, (struct sockaddr *)&from, from_length) == 6);
    CHECK(recv(client, buffer, 4, 0) == 4 && memcmp(buffer, "worl", 4) == 0);

    // connected: send and recv without addresses
    CHECK(connect(client, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    CHECK(send(client, "x", 1, 0) == 1);
    CHECK(recv(server, buffer, sizeof(buffer), 0) == 1 && buffer[0] == 'x');
    CHECK(getpeername(client, (struct sockaddr *)&from, &from_length) == 0 &&
          from.sin_port == addr.sin_port);

    // nothing waits: a non-blocking receive fails at once
    CHECK(recv(server, buffer, sizeof(buffer), MSG_DONTWAIT) == -1 && errno == EAGAIN);

    // broadcast needs SO_BROADCAST
    struct sockaddr_in all = addr;
    all.sin_addr.s_addr = INADDR_BROADCAST;
    int on = 1;
    int value = 0;
    socklen_t value_length = sizeof(value);
    int sender = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(sendto(sender, "b", 1, 0, (struct sockaddr *)&all, sizeof(all)) == -1 &&
          errno == EACCES);
    CHECK(setsockopt(sender, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) == 0);
    CHECK(getsockopt(sender, SOL_SOCKET, SO_BROADCAST, &value, &value_length) == 0 && value == 1);
    CHECK(getsockopt(sender, SOL_SOCKET, SO_TYPE, &value, &value_length) == 0 &&
          value == SOCK_DGRAM);
    close(sender);

    CHECK(shutdown(server, SHUT_RD) == 0 && recv(server, buffer, sizeof(buffer), 0) == 0);
    CHECK(socket(AF_INET, SOCK_STREAM, 0) == -1 && errno == EPROTONOSUPPORT);
    close(server);
    close(client);
}

static void test_ping(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    CHECK(fd >= 0);
    // an echo request: type 8, code 0; the kernel sets the identifier and checksum
    unsigned char request[16] = {8, 0, 0, 0, 0, 0, 0, 1, 'g', 'i', 'n', 'n', 'O', 'S', '!', '!'};
    struct sockaddr_in to = {0};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(sendto(fd, request, sizeof(request), 0, (struct sockaddr *)&to, sizeof(to)) == 16);
    unsigned char reply[64];
    struct pollfd p = {fd, POLLIN, 0};
    CHECK(poll(&p, 1, 1000) == 1);
    CHECK(recv(fd, reply, sizeof(reply), 0) == 16);
    CHECK(reply[0] == 0 && reply[7] == 1 && memcmp(reply + 8, "ginnOS!!", 8) == 0);
    close(fd);
}

static void test_names(void)
{
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    struct addrinfo *list;
    // a name from /etc/hosts, with both socket types
    CHECK(getaddrinfo("localhost", "http", &hints, &list) == 0);
    CHECK(list && list->ai_next && !list->ai_next->ai_next);
    CHECK(list && list->ai_family == AF_INET && list->ai_socktype == SOCK_STREAM &&
          list->ai_next->ai_socktype == SOCK_DGRAM);
    struct sockaddr_in *sin = (struct sockaddr_in *)list->ai_addr;
    CHECK(sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK) && ntohs(sin->sin_port) == 80);
    freeaddrinfo(list);

    // an address as it is; the canonical name is the text
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_CANONNAME;
    CHECK(getaddrinfo("10.0.2.15", "53", &hints, &list) == 0);
    CHECK(list && !list->ai_next && strcmp(list->ai_canonname, "10.0.2.15") == 0);
    sin = (struct sockaddr_in *)list->ai_addr;
    CHECK(sin->sin_addr.s_addr == htonl(0x0A00020F) && ntohs(sin->sin_port) == 53);
    freeaddrinfo(list);

    // no node: any address with AI_PASSIVE, else the loopback one
    hints.ai_flags = AI_PASSIVE;
    CHECK(getaddrinfo(NULL, "8080", &hints, &list) == 0);
    CHECK(((struct sockaddr_in *)list->ai_addr)->sin_addr.s_addr == INADDR_ANY);
    freeaddrinfo(list);

    hints.ai_flags = AI_NUMERICHOST;
    CHECK(getaddrinfo("example.com", NULL, &hints, &list) == EAI_NONAME);
    hints.ai_flags = 0;
    CHECK(getaddrinfo("localhost", "no-such-service", &hints, &list) == EAI_SERVICE);
    hints.ai_family = 10; // AF_INET6
    CHECK(getaddrinfo("localhost", NULL, &hints, &list) == EAI_FAMILY);
    CHECK(strcmp(gai_strerror(EAI_NONAME), "name or service not known") == 0);

    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(443);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    CHECK(getnameinfo((struct sockaddr *)&a, sizeof(a), host, sizeof(host), serv, sizeof(serv),
                      0) == 0);
    CHECK(strcmp(host, "localhost") == 0 && strcmp(serv, "https") == 0);
    CHECK(getnameinfo((struct sockaddr *)&a, sizeof(a), host, sizeof(host), serv, sizeof(serv),
                      NI_NUMERICHOST | NI_NUMERICSERV) == 0);
    CHECK(strcmp(host, "127.0.0.1") == 0 && strcmp(serv, "443") == 0);

    struct hostent *h = gethostbyname("localhost");
    CHECK(h && h->h_addrtype == AF_INET && h->h_length == 4 &&
          memcmp(h->h_addr, &a.sin_addr, 4) == 0 && !h->h_addr_list[1]);
    struct servent *s = getservbyname("ssh", "tcp");
    CHECK(s && s->s_port == htons(22));
}

void test_net(void)
{
    test_names();
    test_addresses();
    test_udp();
    test_ping();
}

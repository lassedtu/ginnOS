/**
 * @file dns.c
 * @brief a DNS client (RFC 1035): A queries over UDP.
 *
 * each name server in /etc/resolv.conf is asked in turn, twice, with a
 * wait of RETRY_MS. the socket is connected to the server, so only its
 * answers come in, and the answer must have the query's id.
 */

#include "dns.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DNS_PORT     53
#define MAX_SERVERS  3
#define ROUNDS       2
#define RETRY_MS     2000
#define MESSAGE_MAX  512 // a UDP answer without EDNS
#define HEADER_SIZE  12
#define TYPE_A       1
#define TYPE_CNAME   5
#define CLASS_IN     1
#define FLAG_RD      0x0100 // recursion desired
#define FLAG_QR      0x8000 // a response
#define RCODE_NXDOMAIN 3
#define RCODE_SERVFAIL 2

static int name_servers(uint32_t *servers)
{
    FILE *f = fopen("/etc/resolv.conf", "r");
    if (!f)
    {
        return 0;
    }
    int count = 0;
    char line[256];
    while (count < MAX_SERVERS && fgets(line, sizeof(line), f))
    {
        char address[64];
        struct in_addr a;
        if (sscanf(line, "nameserver %63s", address) == 1 && inet_pton(AF_INET, address, &a) == 1)
        {
            servers[count++] = a.s_addr;
        }
    }
    fclose(f);
    return count;
}

/**
 * a name as labels: "www.example.com" is 3 www 7 example 3 com 0.
 * @return the length, or 0 if a label is empty or too long.
 */
static size_t encode_name(const char *name, uint8_t *out, size_t room)
{
    size_t at = 0;
    while (*name)
    {
        size_t length = strcspn(name, ".");
        if (length == 0 || length > 63 || at + length + 2 > room)
        {
            return 0;
        }
        out[at++] = (uint8_t)length;
        memcpy(out + at, name, length);
        at += length;
        name += length;
        if (*name == '.')
        {
            name++;
        }
    }
    if (at + 1 > room)
    {
        return 0;
    }
    out[at++] = 0;
    return at;
}

/**
 * read a name at offset, following compression pointers.
 * @param out receives the name with dots (may be NULL).
 * @return the offset after the name where it is in the message, or 0.
 */
static size_t read_name(const uint8_t *m, size_t length, size_t offset, char *out)
{
    size_t end = 0; // where the name ends in place (after the first pointer)
    size_t written = 0;
    for (int jumps = 0; jumps < 32 && offset < length;)
    {
        uint8_t label = m[offset];
        if (label == 0)
        {
            if (out)
            {
                out[written] = '\0';
            }
            return end ? end : offset + 1;
        }
        if ((label & 0xC0) == 0xC0)
        {
            if (offset + 1 >= length)
            {
                return 0;
            }
            if (!end)
            {
                end = offset + 2;
            }
            offset = (size_t)((label & 0x3F) << 8 | m[offset + 1]);
            jumps++;
            continue;
        }
        if ((label & 0xC0) != 0 || offset + 1 + label > length ||
            written + label + 2 > DNS_NAME_MAX)
        {
            return 0;
        }
        if (out)
        {
            if (written > 0)
            {
                out[written++] = '.';
            }
            memcpy(out + written, m + offset + 1, label);
            written += label;
        }
        offset += 1 + label;
    }
    return 0;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

/**
 * read the answers of a response.
 * @return the number of addresses, or an EAI_ error.
 */
static int parse(const uint8_t *m, size_t length, uint16_t id, const char *name,
                 uint32_t *addresses, int max, char *canonical)
{
    if (length < HEADER_SIZE || get16(m) != id || !(get16(m + 2) & FLAG_QR))
    {
        return EAI_AGAIN; // not the answer
    }
    int rcode = get16(m + 2) & 0x0F;
    if (rcode == RCODE_NXDOMAIN)
    {
        return EAI_NONAME;
    }
    if (rcode != 0)
    {
        return rcode == RCODE_SERVFAIL ? EAI_AGAIN : EAI_FAIL;
    }
    uint16_t questions = get16(m + 4);
    uint16_t answers = get16(m + 6);
    size_t at = HEADER_SIZE;
    for (uint16_t i = 0; i < questions; i++)
    {
        at = read_name(m, length, at, NULL);
        if (at == 0 || at + 4 > length)
        {
            return EAI_FAIL;
        }
        at += 4;
    }

    // the answers: CNAMEs move the name along; A records of the name count
    char wanted[DNS_NAME_MAX];
    snprintf(wanted, sizeof(wanted), "%s", name);
    int count = 0;
    for (uint16_t i = 0; i < answers && count < max; i++)
    {
        char owner[DNS_NAME_MAX];
        at = read_name(m, length, at, owner);
        if (at == 0 || at + 10 > length)
        {
            return count > 0 ? count : EAI_FAIL;
        }
        uint16_t type = get16(m + at);
        uint16_t class = get16(m + at + 2);
        uint16_t size = get16(m + at + 8);
        at += 10;
        if (at + size > length)
        {
            break;
        }
        if (class == CLASS_IN && strcasecmp(owner, wanted) == 0)
        {
            if (type == TYPE_CNAME)
            {
                char target[DNS_NAME_MAX];
                if (read_name(m, length, at, target) != 0)
                {
                    snprintf(wanted, sizeof(wanted), "%s", target);
                }
            }
            else if (type == TYPE_A && size == 4)
            {
                memcpy(&addresses[count++], m + at, 4);
            }
        }
        at += size;
    }
    snprintf(canonical, DNS_NAME_MAX, "%s", wanted);
    return count > 0 ? count : EAI_NONAME; // a name without an IPv4 address
}

/**
 * ask one server once.
 * @return the number of addresses, or an EAI_ error (EAI_AGAIN: no answer).
 */
static int ask(uint32_t server, const uint8_t *query, size_t query_length, uint16_t id,
               const char *name, uint32_t *addresses, int max, char *canonical)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        return EAI_SYSTEM;
    }
    struct sockaddr_in to = {0};
    to.sin_family = AF_INET;
    to.sin_port = htons(DNS_PORT);
    to.sin_addr.s_addr = server;
    int result = EAI_AGAIN;
    if (connect(fd, (struct sockaddr *)&to, sizeof(to)) == 0 &&
        send(fd, query, query_length, 0) == (ssize_t)query_length)
    {
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (;;)
        {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long spent = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
            if (spent >= RETRY_MS)
            {
                break;
            }
            struct pollfd p = {fd, POLLIN, 0};
            if (poll(&p, 1, (int)(RETRY_MS - spent)) <= 0)
            {
                continue;
            }
            uint8_t answer[MESSAGE_MAX];
            ssize_t n = recv(fd, answer, sizeof(answer), 0);
            if (n <= 0)
            {
                break; // "port unreachable": no server there
            }
            result = parse(answer, (size_t)n, id, name, addresses, max, canonical);
            if (result != EAI_AGAIN || get16(answer) == id)
            {
                break; // the answer (a SERVFAIL is EAI_AGAIN too)
            }
        }
    }
    close(fd);
    return result;
}

int __dns_lookup(const char *name, uint32_t *addresses, int max, char *canonical)
{
    uint32_t servers[MAX_SERVERS];
    int server_count = name_servers(servers);
    if (server_count == 0)
    {
        return EAI_AGAIN; // no network yet
    }

    uint8_t query[MESSAGE_MAX];
    // a random id: an answer someone made up must guess it (and the port)
    uint16_t id = (uint16_t)arc4random();
    memset(query, 0, HEADER_SIZE);
    query[0] = (uint8_t)(id >> 8);
    query[1] = (uint8_t)id;
    query[2] = FLAG_RD >> 8;
    query[5] = 1; // one question
    size_t length = encode_name(name, query + HEADER_SIZE, sizeof(query) - HEADER_SIZE - 4);
    if (length == 0)
    {
        return EAI_NONAME;
    }
    length += HEADER_SIZE;
    query[length++] = 0;
    query[length++] = TYPE_A;
    query[length++] = 0;
    query[length++] = CLASS_IN;

    int result = EAI_AGAIN;
    for (int round = 0; round < ROUNDS; round++)
    {
        for (int i = 0; i < server_count; i++)
        {
            result = ask(servers[i], query, length, id, name, addresses, max, canonical);
            if (result != EAI_AGAIN)
            {
                return result;
            }
        }
    }
    return result;
}

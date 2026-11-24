/**
 * @file ping.c
 * @brief ping: send ICMP echo requests to a host and show the replies.
 *
 * uses an ICMP datagram socket (SOCK_DGRAM, IPPROTO_ICMP): the kernel
 * sets the identifier and the checksum, so no special rights are needed.
 *
 * usage: ping [-c count] [-i seconds] [-W seconds] address
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DATA_SIZE   56 // as other pings: 64 bytes with the header
#define HEADER_SIZE 8

static volatile sig_atomic_t stop;

static void on_interrupt(int sig)
{
    (void)sig;
    stop = 1;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static void usage(void)
{
    fprintf(stderr, "usage: ping [-c count] [-i seconds] [-W seconds] address\n");
    exit(2);
}

int main(int argc, char **argv)
{
    long count = -1; // until interrupted
    double interval = 1.0;
    double wait = 1.0;
    int opt;
    while ((opt = getopt(argc, argv, "c:i:W:")) != -1)
    {
        switch (opt)
        {
        case 'c':
            count = strtol(optarg, NULL, 10);
            break;
        case 'i':
            interval = strtod(optarg, NULL);
            break;
        case 'W':
            wait = strtod(optarg, NULL);
            break;
        default:
            usage();
        }
    }
    if (optind != argc - 1 || count == 0 || interval < 0.2 || wait <= 0)
    {
        usage();
    }

    struct sockaddr_in to = {0};
    to.sin_family = AF_INET;
    if (inet_pton(AF_INET, argv[optind], &to.sin_addr) != 1)
    {
        fprintf(stderr, "ping: %s: not an IPv4 address\n", argv[optind]);
        return 2;
    }
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0)
    {
        perror("ping: socket");
        return 2;
    }

    struct sigaction sa = {0};
    sa.sa_handler = on_interrupt;
    sigaction(SIGINT, &sa, NULL);

    char address[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &to.sin_addr, address, sizeof(address));
    printf("PING %s: %d data bytes\n", address, DATA_SIZE);

    long sent = 0;
    long received = 0;
    double min = 0;
    double max = 0;
    double sum = 0;
    for (uint16_t seq = 0; !stop && (count < 0 || sent < count); seq++)
    {
        unsigned char request[HEADER_SIZE + DATA_SIZE];
        memset(request, 0, sizeof(request));
        request[0] = 8; // echo request
        request[6] = (unsigned char)(seq >> 8);
        request[7] = (unsigned char)seq;
        for (int i = 0; i < DATA_SIZE; i++)
        {
            request[HEADER_SIZE + i] = (unsigned char)i;
        }
        double start = now_ms();
        if (sendto(fd, request, sizeof(request), 0, (struct sockaddr *)&to, sizeof(to)) < 0)
        {
            printf("ping: %s: %s\n", address, strerror(errno));
        }
        sent++;

        // replies until the wait is over; an old reply is shown as it is
        bool answered = false;
        double deadline = start + wait * 1000.0;
        while (!stop && !answered)
        {
            double left = deadline - now_ms();
            if (left <= 0)
            {
                break;
            }
            struct pollfd p = {fd, POLLIN, 0};
            if (poll(&p, 1, (int)left + 1) <= 0)
            {
                continue;
            }
            unsigned char reply[HEADER_SIZE + DATA_SIZE + 64];
            struct sockaddr_in from;
            socklen_t from_length = sizeof(from);
            ssize_t n = recvfrom(fd, reply, sizeof(reply), 0, (struct sockaddr *)&from,
                                 &from_length);
            if (n < HEADER_SIZE || reply[0] != 0)
            {
                continue;
            }
            uint16_t reply_seq = (uint16_t)((reply[6] << 8) | reply[7]);
            double ms = now_ms() - start;
            char from_text[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, from_text, sizeof(from_text));
            printf("%zd bytes from %s: icmp_seq=%u time=%.1f ms\n", n, from_text, reply_seq, ms);
            if (reply_seq == seq)
            {
                answered = true;
                received++;
                sum += ms;
                min = received == 1 || ms < min ? ms : min;
                max = ms > max ? ms : max;
            }
        }

        // the rest of the interval before the next request
        double next = start + interval * 1000.0;
        while (!stop && (count < 0 || sent < count) && now_ms() < next)
        {
            poll(NULL, 0, (int)(next - now_ms()) + 1);
        }
    }

    printf("--- %s ping statistics ---\n", address);
    printf("%ld packets transmitted, %ld received, %ld%% packet loss\n", sent, received,
           sent ? (sent - received) * 100 / sent : 0);
    if (received > 0)
    {
        printf("round-trip min/avg/max = %.1f/%.1f/%.1f ms\n", min, sum / (double)received, max);
    }
    close(fd);
    return received > 0 ? 0 : 1;
}

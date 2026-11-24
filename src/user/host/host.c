/**
 * @file host.c
 * @brief host: look up the IPv4 addresses of host names.
 *
 * usage: host name...
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: host name...\n");
        return 2;
    }
    int status = 0;
    for (int i = 1; i < argc; i++)
    {
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM; // one entry for each address
        hints.ai_flags = AI_CANONNAME;
        struct addrinfo *list;
        int err = getaddrinfo(argv[i], NULL, &hints, &list);
        if (err != 0)
        {
            printf("host %s not found: %s\n", argv[i], gai_strerror(err));
            status = 1;
            continue;
        }
        const char *name = argv[i];
        if (list->ai_canonname && strcasecmp(list->ai_canonname, argv[i]) != 0)
        {
            printf("%s is an alias for %s.\n", argv[i], list->ai_canonname);
            name = list->ai_canonname;
        }
        for (struct addrinfo *ai = list; ai; ai = ai->ai_next)
        {
            char text[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in *)ai->ai_addr)->sin_addr, text, sizeof(text));
            printf("%s has address %s\n", name, text);
        }
        freeaddrinfo(list);
    }
    return status;
}

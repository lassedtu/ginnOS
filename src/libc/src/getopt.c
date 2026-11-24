/**
 * @file getopt.c
 * @brief getopt (POSIX): the options of a command line, one at a time.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

char *optarg;
int optind = 1;
int opterr = 1;
int optopt;

// the place in the current argument (for grouped options such as -ab)
static int next_char;

int getopt(int argc, char *const argv[], const char *options)
{
    bool quiet = options[0] == ':';
    optarg = NULL;
    if (optind == 0)
    {
        optind = 1; // a reset (as glibc and musl do)
        next_char = 0;
    }
    if (next_char == 0)
    {
        if (optind >= argc || !argv[optind] || argv[optind][0] != '-' || argv[optind][1] == '\0')
        {
            return -1;
        }
        if (strcmp(argv[optind], "--") == 0)
        {
            optind++;
            return -1;
        }
        next_char = 1;
    }

    char *arg = argv[optind];
    int c = (unsigned char)arg[next_char++];
    const char *spec = c == ':' ? NULL : strchr(options + quiet, c);
    if (arg[next_char] == '\0')
    {
        optind++;
        next_char = 0;
    }
    if (!spec)
    {
        optopt = c;
        if (opterr && !quiet)
        {
            fprintf(stderr, "%s: unknown option -%c\n", argv[0], c);
        }
        return '?';
    }
    if (spec[1] != ':')
    {
        return c;
    }

    // an argument: the rest of this one (-c3), or the next one (-c 3)
    if (next_char != 0)
    {
        optarg = arg + next_char;
        optind++;
        next_char = 0;
    }
    else if (optind < argc)
    {
        optarg = argv[optind++];
    }
    else
    {
        optopt = c;
        if (quiet)
        {
            return ':';
        }
        if (opterr)
        {
            fprintf(stderr, "%s: option -%c needs an argument\n", argv[0], c);
        }
        return '?';
    }
    return c;
}

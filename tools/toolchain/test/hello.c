/*
 * a test of the ginnOS toolchain: built with no special flags,
 *   x86_64-ginnos-gcc -O2 hello.c -o hello
 * it must run on ginnOS.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
#ifdef __ginnos__
    const char *os = "ginnOS";
#else
    const char *os = "not ginnOS";
#endif
    int64_t big = INT64_MAX;
    char *copy = strdup(argv[0]);
    printf("hello from %s: %d argument(s), %s, %" PRId64 "\n", os, argc, copy, big);
    free(copy);
    return 0;
}

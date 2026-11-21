/**
 * @file chmod.c
 * @brief Change file modes: chmod <octal mode> <file>...
 */

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("usage: chmod <octal mode> <file>...\n");
        return 1;
    }

    unsigned int mode = 0;
    for (const char *c = argv[1]; *c; c++)
    {
        if (*c < '0' || *c > '7')
        {
            printf("chmod: invalid mode '%s'\n", argv[1]);
            return 1;
        }
        mode = mode * 8 + (unsigned int)(*c - '0');
    }

    int status = 0;
    for (int i = 2; i < argc; i++)
    {
        if (chmod(argv[i], mode) < 0)
        {
            perror(argv[i]);
            status = 1;
        }
    }
    return status;
}

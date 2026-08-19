/**
 * @file rm.c
 * @brief Remove files.
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("rm: usage: rm <file> [file...]\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++)
    {
        if (unlink(argv[i]) < 0)
        {
            printf("rm: cannot remove '%s'\n", argv[i]);
            ret = 1;
        }
    }

    return ret;
}

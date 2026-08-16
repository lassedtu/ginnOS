/**
 * @file rmdir.c
 * @brief Remove directories.
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("rmdir: usage: rmdir <directory> [directory...]\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++)
    {
        if (rmdir(argv[i]) < 0)
        {
            printf("rmdir: cannot remove '%s'\n", argv[i]);
            ret = 1;
        }
    }

    return ret;
}

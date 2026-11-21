/**
 * @file touch.c
 * @brief Create empty files. existing files are left as they are (there
 * are no file times to update yet).
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("touch: usage: touch <file> [file...]\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++)
    {
        // create the file if it is missing; an existing one keeps its data
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);
        if (fd < 0)
        {
            printf("touch: cannot create '%s'\n", argv[i]);
            ret = 1;
            continue;
        }
        close(fd);
    }

    return ret;
}

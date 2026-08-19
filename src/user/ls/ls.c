/**
 * @file ls.c
 * @brief List directory contents.
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *path = ".";

    if (argc >= 2)
        path = argv[1];

    int fd = open(path, 0);
    if (fd < 0)
    {
        printf("ls: cannot open '%s'\n", path);
        return 1;
    }

    dirent_t entry;

    while (readdir(fd, &entry) == 0)
    {
        // skip . and ..
        if (entry.name[0] == '.' &&
            (entry.name[1] == '\0' ||
             (entry.name[1] == '.' && entry.name[2] == '\0')))
            continue;

        if (entry.file_type == FT_DIR)
            printf("%s/\n", entry.name);
        else
            printf("%s\n", entry.name);
    }

    close(fd);
    return 0;
}

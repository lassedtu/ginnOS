/**
 * @file ls.c
 * @brief List directory contents.
 */

#include <dirent.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *path = ".";

    if (argc >= 2)
        path = argv[1];

    DIR *dir = opendir(path);
    if (!dir)
    {
        printf("ls: cannot open '%s'\n", path);
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // skip . and ..
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;

        if (entry->d_type == DT_DIR)
            printf("%s/\n", entry->d_name);
        else
            printf("%s\n", entry->d_name);
    }

    closedir(dir);
    return 0;
}

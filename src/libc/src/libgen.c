/**
 * @file libgen.c
 * @brief basename and dirname (POSIX: they may change the path).
 */

#include "libgen.h"

#include "string.h"

char *basename(char *path)
{
    static char dot[] = ".";
    if (!path || !*path)
        return dot;
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/')
        path[--end] = '\0'; // "lib/" is "lib"
    char *slash = strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}

char *dirname(char *path)
{
    static char dot[] = ".";
    static char root[] = "/";
    if (!path || !*path)
        return dot;
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/')
        end--; // the slashes at the end
    while (end > 0 && path[end - 1] != '/')
        end--; // the last part
    if (end == 0)
        return dot;
    while (end > 1 && path[end - 1] == '/')
        end--; // the slashes before it
    path[end] = '\0';
    return path[0] ? path : root;
}

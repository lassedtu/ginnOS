/**
 * @file folder.c
 * @brief Freyja: the pictures in the folder of the open one.
 */

#include "folder.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *const kinds[] = {".png", ".jpg", ".jpeg", ".bmp", ".ppm", ".pgm"};

bool folder_is_picture(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name)
        return false;
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++)
        if (strcasecmp(dot, kinds[i]) == 0)
            return true;
    return false;
}

static int by_name(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

void folder_free(folder_t *f)
{
    for (size_t i = 0; i < f->count; i++)
        free(f->names[i]);
    free(f->names);
    free(f->folder);
    memset(f, 0, sizeof(*f));
}

bool folder_read(folder_t *f, const char *path)
{
    folder_free(f);
    const char *slash = strrchr(path, '/');
    if (!slash)
        return false;
    const char *name = slash + 1;
    f->folder = strndup(path, (size_t)(slash - path));
    DIR *dir = opendir(f->folder[0] ? f->folder : "/");
    if (!f->folder || !dir)
    {
        if (dir)
            closedir(dir);
        folder_free(f);
        return false;
    }
    size_t capacity = 0;
    struct dirent *e;
    bool ok = true;
    while (ok && (e = readdir(dir)))
    {
        if (e->d_name[0] == '.' || !folder_is_picture(e->d_name))
            continue;
        if (f->count == capacity)
        {
            capacity = capacity ? capacity * 2 : 32;
            char **more = realloc(f->names, capacity * sizeof(*more));
            if (!more)
            {
                ok = false;
                break;
            }
            f->names = more;
        }
        f->names[f->count] = strdup(e->d_name);
        ok = f->names[f->count] != NULL;
        f->count += ok;
    }
    closedir(dir);
    if (!ok)
    {
        folder_free(f);
        return false;
    }
    qsort(f->names, f->count, sizeof(*f->names), by_name);
    for (size_t i = 0; i < f->count; i++)
        if (strcmp(f->names[i], name) == 0)
            f->current = i;
    return true;
}

char *folder_step(folder_t *f, int step)
{
    if (f->count < 2)
        return NULL;
    long n = (long)f->count;
    f->current = (size_t)((((long)f->current + step) % n + n) % n);
    size_t length = strlen(f->folder) + strlen(f->names[f->current]) + 2;
    char *path = malloc(length);
    if (path)
        snprintf(path, length, "%s/%s", f->folder, f->names[f->current]);
    return path;
}

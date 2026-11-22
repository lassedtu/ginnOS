/**
 * @file folder.c
 * @brief yggdrasil: reading a folder: names, sizes, times and file types,
 *        folders first, then by name.
 */

#include "yggdrasil.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int compare_names(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
    {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d)
            return d;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int compare(const void *pa, const void *pb)
{
    const entry_t *a = pa;
    const entry_t *b = pb;
    bool da = S_ISDIR(a->mode);
    bool db = S_ISDIR(b->mode);
    if (da != db)
        return da ? -1 : 1;
    int d = compare_names(a->name, b->name);
    return d ? d : strcmp(a->name, b->name);
}

/** the type and the icon of an entry; device files are not opened */
static void find_type(entry_t *e, const char *path, const mime_table_t *mime)
{
    if (S_ISDIR(e->mode))
    {
        e->type = "inode/directory";
        e->icon = folder_icon(path);
        return;
    }
    if (!S_ISREG(e->mode))
    {
        e->type = S_ISCHR(e->mode) ? "inode/chardevice" : S_ISBLK(e->mode) ? "inode/blockdevice"
                                                                           : "inode/fifo";
        e->icon = S_ISBLK(e->mode) ? "drive-harddisk" : "unknown";
        return;
    }
    const mime_type_t *t = mime ? mime_lookup_file(mime, path) : NULL;
    e->type = t ? t->type : "application/octet-stream";
    e->icon = t ? t->icon : "unknown";
}

bool folder_read(folder_t *f, const char *path, bool hidden, const mime_table_t *mime)
{
    folder_free(f);
    snprintf(f->path, sizeof(f->path), "%s", path);
    DIR *dir = opendir(path);
    if (!dir)
    {
        f->error = errno;
        return false;
    }
    int capacity = 0;
    struct dirent *d;
    while ((d = readdir(dir)))
    {
        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
            continue;
        if (!hidden && d->d_name[0] == '.')
            continue;
        if (f->count == capacity)
        {
            capacity = capacity ? capacity * 2 : 64;
            entry_t *more = realloc(f->entries, (size_t)capacity * sizeof(entry_t));
            if (!more)
                break;
            f->entries = more;
        }
        char full[PATH_LEN];
        path_join(path, d->d_name, full, sizeof(full));
        struct stat st;
        if (lstat(full, &st) != 0)
            continue; // gone since readdir
        entry_t *e = &f->entries[f->count];
        *e = (entry_t){0};
        e->name = strdup(d->d_name);
        if (!e->name)
            break;
        e->mode = st.st_mode;
        e->uid = st.st_uid;
        e->gid = st.st_gid;
        e->size = st.st_size;
        e->mtime = st.st_mtime;
        find_type(e, full, mime);
        f->count++;
    }
    closedir(dir);
    qsort(f->entries, (size_t)f->count, sizeof(entry_t), compare);
    return true;
}

void folder_free(folder_t *f)
{
    for (int i = 0; i < f->count; i++)
        free(f->entries[i].name);
    free(f->entries);
    f->entries = NULL;
    f->count = 0;
    f->error = 0;
}

bool folder_same(const folder_t *a, const folder_t *b)
{
    if (a->count != b->count || a->error != b->error || strcmp(a->path, b->path) != 0)
        return false;
    for (int i = 0; i < a->count; i++)
    {
        const entry_t *x = &a->entries[i];
        const entry_t *y = &b->entries[i];
        if (strcmp(x->name, y->name) != 0 || x->size != y->size || x->mtime != y->mtime ||
            x->mode != y->mode)
            return false;
    }
    return true;
}

void format_size(uint64_t size, char *out, size_t length)
{
    static const char *const units[] = {"KiB", "MiB", "GiB", "TiB"};
    if (size < 1024)
    {
        snprintf(out, length, "%u %s", (unsigned)size, size == 1 ? "byte" : "bytes");
        return;
    }
    // tenths, without floating point
    uint64_t tenths = size * 10 / 1024;
    int unit = 0;
    while (tenths >= 10240 && unit < 3)
    {
        tenths /= 1024;
        unit++;
    }
    if (tenths >= 1000)
        snprintf(out, length, "%u %s", (unsigned)((tenths + 5) / 10), units[unit]);
    else
        snprintf(out, length, "%u.%u %s", (unsigned)(tenths / 10), (unsigned)(tenths % 10),
                 units[unit]);
}

void format_time(uint32_t time, char *out, size_t length)
{
    time_t t = (time_t)time;
    struct tm tm;
    if (!localtime_r(&t, &tm))
    {
        snprintf(out, length, "?");
        return;
    }
    strftime(out, length, "%Y-%m-%d %H:%M", &tm);
}

const char *folder_icon(const char *path)
{
    static const struct
    {
        const char *name;
        const char *icon;
    } special[] = {
        {"Desktop", "user-desktop"},     {"Documents", "folder-documents"},
        {"Downloads", "folder-download"}, {"Pictures", "folder-pictures"},
        {"Music", "folder-music"},
    };
    if (strcmp(path, Y.home) == 0)
        return "user-home";
    if (strcmp(path, Y.trash) == 0 || (strncmp(path, Y.trash, strlen(Y.trash)) == 0 &&
                                       strcmp(path + strlen(Y.trash), "/files") == 0))
        return "user-trash";
    char parent[PATH_LEN];
    path_parent(path, parent, sizeof(parent));
    if (strcmp(parent, Y.home) == 0)
    {
        const char *name = path_name(path);
        for (size_t i = 0; i < sizeof(special) / sizeof(special[0]); i++)
            if (strcmp(name, special[i].name) == 0)
                return special[i].icon;
    }
    return "folder";
}

void path_join(const char *dir, const char *name, char *out, size_t length)
{
    size_t n = strlen(dir);
    snprintf(out, length, "%s%s%s", dir, n && dir[n - 1] == '/' ? "" : "/", name);
}

void path_parent(const char *path, char *out, size_t length)
{
    snprintf(out, length, "%s", path);
    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/')
        out[--n] = '\0';
    char *slash = strrchr(out, '/');
    if (!slash || slash == out)
        snprintf(out, length, "/");
    else
        *slash = '\0';
}

const char *path_name(const char *path)
{
    if (strcmp(path, "/") == 0)
        return "/";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

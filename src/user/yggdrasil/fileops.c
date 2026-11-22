/**
 * @file fileops.c
 * @brief yggdrasil: copy, move and delete folder trees, and the trash.
 */

#include "fileops.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COPY_BUFFER (64 * 1024)
#define FILEOPS_PATH 1024

static bool stopped(const fileops_report_t *r)
{
    return r && r->stop && *r->stop;
}

static void tell_file(const fileops_report_t *r, const char *path)
{
    if (r && r->file)
        r->file(path, r->data);
}

static void tell_bytes(const fileops_report_t *r, uint64_t count)
{
    if (r && r->bytes)
        r->bytes(count, r->data);
}

/** report an error and keep the first one in *first */
static void tell_error(const fileops_report_t *r, const char *path, int error, int *first)
{
    if (r && r->error)
        r->error(path, error, r->data);
    if (*first == 0)
        *first = error;
}

static bool join(const char *dir, const char *name, char *out, size_t length)
{
    int n = snprintf(out, length, "%s%s%s", dir, dir[strlen(dir) - 1] == '/' ? "" : "/", name);
    return n > 0 && (size_t)n < length;
}

static bool dots(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

uint64_t fileops_size(const char *path, int *files)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return 0;
    if (!S_ISDIR(st.st_mode))
    {
        if (files)
            (*files)++;
        return (uint64_t)st.st_size;
    }
    uint64_t total = 0;
    DIR *dir = opendir(path);
    struct dirent *e;
    char child[FILEOPS_PATH];
    while (dir && (e = readdir(dir)))
    {
        if (!dots(e->d_name) && join(path, e->d_name, child, sizeof(child)))
            total += fileops_size(child, files);
    }
    if (dir)
        closedir(dir);
    return total;
}

static int copy_file(const char *from, const char *to, mode_t mode, const fileops_report_t *r)
{
    int in = open(from, O_RDONLY);
    if (in < 0)
        return errno;
    int out = open(to, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);
    if (out < 0)
    {
        int error = errno;
        close(in);
        return error;
    }
    static char buffer[COPY_BUFFER];
    int error = 0;
    for (;;)
    {
        ssize_t n = read(in, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
        {
            error = errno;
            break;
        }
        if (n == 0)
            break;
        if (stopped(r))
        {
            error = ECANCELED;
            break;
        }
        for (ssize_t done = 0; done < n;)
        {
            ssize_t w = write(out, buffer + done, (size_t)(n - done));
            if (w < 0 && errno == EINTR)
                continue;
            if (w <= 0)
            {
                error = w < 0 ? errno : ENOSPC;
                break;
            }
            done += w;
        }
        if (error)
            break;
        tell_bytes(r, (uint64_t)n);
    }
    close(in);
    if (close(out) != 0 && error == 0)
        error = errno;
    if (error)
        unlink(to); // no half file stays
    return error;
}

int fileops_copy(const char *from, const char *to, const fileops_report_t *report)
{
    int first = 0;
    struct stat st;
    if (lstat(from, &st) != 0)
    {
        tell_error(report, from, errno, &first);
        return first;
    }
    tell_file(report, from);
    if (!S_ISDIR(st.st_mode))
    {
        int error = copy_file(from, to, st.st_mode, report);
        if (error)
            tell_error(report, from, error, &first);
        return first;
    }
    if (mkdir(to, (st.st_mode & 0777) | 0700) != 0)
    {
        tell_error(report, to, errno, &first);
        return first;
    }
    DIR *dir = opendir(from);
    if (!dir)
    {
        tell_error(report, from, errno, &first);
        return first;
    }
    struct dirent *e;
    char a[FILEOPS_PATH];
    char b[FILEOPS_PATH];
    while ((e = readdir(dir)) && !stopped(report))
    {
        if (dots(e->d_name))
            continue;
        if (!join(from, e->d_name, a, sizeof(a)) || !join(to, e->d_name, b, sizeof(b)))
        {
            tell_error(report, e->d_name, ENAMETOOLONG, &first);
            continue;
        }
        int error = fileops_copy(a, b, report);
        if (error && first == 0)
            first = error;
    }
    closedir(dir);
    chmod(to, st.st_mode & 0777); // the folder's own mode, after its files are in
    return first;
}

int fileops_remove(const char *path, const fileops_report_t *report)
{
    int first = 0;
    struct stat st;
    if (lstat(path, &st) != 0)
    {
        tell_error(report, path, errno, &first);
        return first;
    }
    tell_file(report, path);
    if (S_ISDIR(st.st_mode))
    {
        DIR *dir = opendir(path);
        struct dirent *e;
        char child[FILEOPS_PATH];
        while (dir && (e = readdir(dir)) && !stopped(report))
        {
            if (dots(e->d_name))
                continue;
            if (!join(path, e->d_name, child, sizeof(child)))
            {
                tell_error(report, e->d_name, ENAMETOOLONG, &first);
                continue;
            }
            int error = fileops_remove(child, report);
            if (error && first == 0)
                first = error;
        }
        if (dir)
            closedir(dir);
        else
            tell_error(report, path, errno, &first);
        if (rmdir(path) != 0)
            tell_error(report, path, errno, &first);
        return first;
    }
    if (unlink(path) != 0)
        tell_error(report, path, errno, &first);
    else
        tell_bytes(report, (uint64_t)st.st_size);
    return first;
}

int fileops_move(const char *from, const char *to, const fileops_report_t *report)
{
    tell_file(report, from);
    if (rename(from, to) == 0)
    {
        tell_bytes(report, fileops_size(to, NULL));
        return 0;
    }
    int first = 0;
    if (errno != EXDEV)
    {
        tell_error(report, from, errno, &first);
        return first;
    }
    // another disk: copy, and delete only when all of it is there
    first = fileops_copy(from, to, report);
    if (first == 0)
        first = fileops_remove(from, NULL);
    return first;
}

void fileops_unique(const char *dir, const char *name, char *out, size_t length)
{
    join(dir, name, out, length);
    struct stat st;
    if (lstat(out, &st) != 0)
        return;
    // the extension stays at the end; a name that starts with a dot has none
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name)
        dot = name + strlen(name);
    int stem = (int)(dot - name);
    for (int n = 2; n < 10000; n++)
    {
        char candidate[FILEOPS_PATH];
        snprintf(candidate, sizeof(candidate), "%.*s (%d)%s", stem, name, n, dot);
        join(dir, candidate, out, length);
        if (lstat(out, &st) != 0)
            return;
    }
}

bool fileops_inside(const char *path, const char *dir)
{
    size_t n = strlen(dir);
    while (n > 1 && dir[n - 1] == '/')
        n--;
    if (strncmp(path, dir, n) != 0)
        return false;
    return path[n] == '\0' || path[n] == '/' || (n == 1 && dir[0] == '/');
}

/** make one folder unless it is there (mkdir of "/" fails differently) */
static int make_one(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : ENOTDIR;
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return errno;
    return 0;
}

int fileops_mkdirs(const char *path)
{
    char copy[FILEOPS_PATH];
    if (snprintf(copy, sizeof(copy), "%s", path) >= (int)sizeof(copy))
        return ENAMETOOLONG;
    for (char *p = copy + 1; *p; p++)
    {
        if (*p != '/')
            continue;
        *p = '\0';
        int error = make_one(copy);
        if (error)
            return error;
        *p = '/';
    }
    return make_one(copy);
}

int trash_put(const char *trash, const char *path, time_t now)
{
    char files[FILEOPS_PATH];
    char info[FILEOPS_PATH];
    snprintf(files, sizeof(files), "%s/files", trash);
    snprintf(info, sizeof(info), "%s/info", trash);
    int error = fileops_mkdirs(files);
    if (error == 0)
        error = fileops_mkdirs(info);
    if (error)
        return error;

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char target[FILEOPS_PATH];
    fileops_unique(files, base, target, sizeof(target));
    const char *name = strrchr(target, '/') + 1;

    char info_path[FILEOPS_PATH];
    snprintf(info_path, sizeof(info_path), "%s/%s.trashinfo", info, name);
    FILE *f = fopen(info_path, "w");
    if (!f)
        return errno;
    struct tm tm;
    localtime_r(&now, &tm);
    char date[32];
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &tm);
    fprintf(f, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", path, date);
    if (fclose(f) != 0)
    {
        error = errno;
        unlink(info_path);
        return error;
    }
    if (rename(path, target) != 0)
    {
        error = errno;
        unlink(info_path);
        return error;
    }
    return 0;
}

int trash_restore(const char *trash, const char *name, char *restored, size_t length)
{
    char info_path[FILEOPS_PATH];
    snprintf(info_path, sizeof(info_path), "%s/info/%s.trashinfo", trash, name);
    FILE *f = fopen(info_path, "r");
    if (!f)
        return errno;
    char line[FILEOPS_PATH + 16];
    char original[FILEOPS_PATH] = "";
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "Path=", 5) == 0)
        {
            snprintf(original, sizeof(original), "%s", line + 5);
            original[strcspn(original, "\n")] = '\0';
        }
    }
    fclose(f);
    if (original[0] != '/')
        return EINVAL;

    // the folder it was in, made again if it is gone
    char parent[FILEOPS_PATH];
    snprintf(parent, sizeof(parent), "%s", original);
    char *slash = strrchr(parent, '/');
    if (slash == parent)
        slash[1] = '\0';
    else
        *slash = '\0';
    int error = fileops_mkdirs(parent);
    if (error)
        return error;
    fileops_unique(parent, strrchr(original, '/') + 1, restored, length);

    char from[FILEOPS_PATH];
    snprintf(from, sizeof(from), "%s/files/%s", trash, name);
    if (rename(from, restored) != 0)
        return errno;
    unlink(info_path);
    return 0;
}

int trash_empty(const char *trash)
{
    int first = 0;
    const char *parts[] = {"files", "info"};
    for (int i = 0; i < 2; i++)
    {
        char dir_path[FILEOPS_PATH];
        snprintf(dir_path, sizeof(dir_path), "%s/%s", trash, parts[i]);
        DIR *dir = opendir(dir_path);
        struct dirent *e;
        char child[FILEOPS_PATH];
        while (dir && (e = readdir(dir)))
        {
            if (dots(e->d_name) || !join(dir_path, e->d_name, child, sizeof(child)))
                continue;
            int error = fileops_remove(child, NULL);
            if (error && first == 0)
                first = error;
        }
        if (dir)
            closedir(dir);
    }
    return first;
}

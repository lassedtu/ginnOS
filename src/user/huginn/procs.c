/**
 * @file procs.c
 * @brief Huginn: a sample of the processes and the machine, from /proc.
 */

#include "procs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** the text after "key:" in a status line, or NULL */
static const char *value_of(const char *line, const char *key)
{
    size_t n = strlen(key);
    if (strncmp(line, key, n) != 0 || line[n] != ':')
        return NULL;
    line += n + 1;
    while (*line == ' ' || *line == '\t')
        line++;
    return line;
}

/** read /proc/<pid>/status; false when the process has gone */
static bool read_status(const char *root, uint32_t pid, proc_t *p)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%u/status", root, pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    memset(p, 0, sizeof(*p));
    p->pid = pid;
    uint32_t utime = 0;
    uint32_t stime = 0;
    char line[160];
    const char *v;
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\n")] = '\0';
        if ((v = value_of(line, "Name")))
            snprintf(p->name, sizeof(p->name), "%s", v);
        else if ((v = value_of(line, "State")))
            snprintf(p->state, sizeof(p->state), "%s", v);
        else if ((v = value_of(line, "PPid")))
            p->ppid = (uint32_t)strtoul(v, NULL, 10);
        else if ((v = value_of(line, "Uid")))
            p->uid = (uint32_t)strtoul(v, NULL, 10);
        else if ((v = value_of(line, "VmRSS")))
            p->rss_kb = (uint32_t)strtoul(v, NULL, 10);
        else if ((v = value_of(line, "Utime")))
            utime = (uint32_t)strtoul(v, NULL, 10);
        else if ((v = value_of(line, "Stime")))
            stime = (uint32_t)strtoul(v, NULL, 10);
    }
    fclose(f);
    p->ticks = utime + stime;
    return true;
}

static void read_machine(sample_t *s, const char *root)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/uptime", root);
    FILE *f = fopen(path, "r");
    if (f)
    {
        // "12.34 10.50": the time since boot, and the idle part of it
        char line[64];
        if (fgets(line, sizeof(line), f))
        {
            char *end;
            s->uptime = strtod(line, &end);
            s->idle = strtod(end, NULL);
        }
        fclose(f);
    }
    snprintf(path, sizeof(path), "%s/meminfo", root);
    f = fopen(path, "r");
    if (f)
    {
        char line[160];
        const char *v;
        while (fgets(line, sizeof(line), f))
        {
            if ((v = value_of(line, "MemTotal")))
                s->mem_total_kb = (uint32_t)strtoul(v, NULL, 10);
            else if ((v = value_of(line, "MemFree")))
                s->mem_free_kb = (uint32_t)strtoul(v, NULL, 10);
        }
        fclose(f);
    }
}

static int by_pid(const void *a, const void *b)
{
    uint32_t x = ((const proc_t *)a)->pid;
    uint32_t y = ((const proc_t *)b)->pid;
    return x < y ? -1 : x > y;
}

const proc_t *sample_find(const sample_t *s, uint32_t pid)
{
    proc_t key = {.pid = pid};
    return s->count ? bsearch(&key, s->procs, (size_t)s->count, sizeof(proc_t), by_pid) : NULL;
}

bool sample_read(sample_t *s, const char *root, const sample_t *before)
{
    memset(s, 0, sizeof(*s));
    DIR *dir = opendir(root);
    if (!dir)
        return false;
    read_machine(s, root);
    int capacity = 0;
    struct dirent *d;
    while ((d = readdir(dir)))
    {
        char *end;
        unsigned long pid = strtoul(d->d_name, &end, 10);
        if (*end != '\0' || pid == 0)
            continue; // not a process ("self", "meminfo" ...)
        if (s->count == capacity)
        {
            capacity = capacity ? capacity * 2 : 64;
            proc_t *more = realloc(s->procs, (size_t)capacity * sizeof(proc_t));
            if (!more)
                break;
            s->procs = more;
        }
        if (read_status(root, (uint32_t)pid, &s->procs[s->count]))
            s->count++;
    }
    closedir(dir);
    qsort(s->procs, (size_t)s->count, sizeof(proc_t), by_pid);

    // the CPU parts: what was used since the sample before
    double span = before ? s->uptime - before->uptime : 0;
    if (span > 0)
    {
        double busy = 1.0 - (s->idle - before->idle) / span;
        s->cpu = busy < 0 ? 0 : busy > 1 ? 100 : busy * 100;
        for (int i = 0; i < s->count; i++)
        {
            const proc_t *old = sample_find(before, s->procs[i].pid);
            if (!old || old->ticks > s->procs[i].ticks)
                continue; // new since then (or a new process with an old pid)
            double used = (s->procs[i].ticks - old->ticks) / (double)TICKS_PER_SECOND;
            double part = used / span * 100;
            s->procs[i].cpu = part > 100 ? 100 : part;
        }
    }
    return true;
}

void sample_free(sample_t *s)
{
    free(s->procs);
    memset(s, 0, sizeof(*s));
}

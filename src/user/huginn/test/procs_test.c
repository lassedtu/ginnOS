/**
 * @file procs_test.c
 * @brief host tests of Huginn's samples, from a /proc made of files.
 */

#include "../procs.h"
#include "hosttest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/huginn-proc-XXXXXX";

static void write_file(const char *name, const char *text)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *f = fopen(path, "w");
    if (f)
    {
        fputs(text, f);
        fclose(f);
    }
}

static void process(uint32_t pid, const char *name, uint32_t utime, uint32_t stime)
{
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/%u", root, pid);
    mkdir(dir, 0755);
    char text[400];
    snprintf(text, sizeof(text),
             "Name:\t%s\nState:\tsleeping\nPid:\t%u\nPPid:\t1\nUid:\t%u\nAreas:\t3\n"
             "Fds:\t3\nBrk:\t0x40a000\nVmRSS:\t%u kB\nUtime:\t%u\nStime:\t%u\n",
             name, pid, pid == 9 ? 1000 : 0, pid * 100, utime, stime);
    char file[64];
    snprintf(file, sizeof(file), "%u/status", pid);
    write_file(file, text);
}

int main(void)
{
    CHECK(mkdtemp(root) != NULL);
    char self[300];
    snprintf(self, sizeof(self), "%s/self", root);
    mkdir(self, 0755); // a folder that is not a process
    write_file("uptime", "10.00 8.00\n");
    write_file("meminfo", "MemTotal:\t131072 kB\nMemFree:\t65536 kB\nHeapUsed:\t10 kB\n");
    process(1, "init", 5, 1);
    process(9, "mimir", 100, 20);

    sample_t first;
    CHECK(sample_read(&first, root, NULL));
    CHECK_EQ(first.count, 2); // "self" is not a process
    CHECK_EQ(first.mem_total_kb, 131072);
    CHECK_EQ(first.mem_free_kb, 65536);
    CHECK(first.uptime > 9.99 && first.uptime < 10.01);
    const proc_t *m = sample_find(&first, 9);
    CHECK(m && strcmp(m->name, "mimir") == 0);
    CHECK_EQ(m->uid, 1000);
    CHECK_EQ(m->rss_kb, 900);
    CHECK_EQ(m->ticks, 120);
    CHECK(m->cpu == 0); // no sample before
    CHECK(strcmp(m->state, "sleeping") == 0);

    // two seconds later: idle 1 of 2 seconds; mimir used 0.5 s, init none
    write_file("uptime", "12.00 9.00\n");
    process(9, "mimir", 140, 30);
    process(12, "saga", 3, 0);
    sample_t second;
    CHECK(sample_read(&second, root, &first));
    CHECK_EQ(second.count, 3);
    CHECK(second.cpu > 49.9 && second.cpu < 50.1);
    m = sample_find(&second, 9);
    CHECK(m && m->cpu > 24.9 && m->cpu < 25.1);
    const proc_t *init = sample_find(&second, 1);
    CHECK(init && init->cpu == 0);
    const proc_t *saga = sample_find(&second, 12);
    CHECK(saga && saga->cpu == 0); // new: nothing to compare with
    CHECK(sample_find(&second, 7) == NULL);

    sample_free(&first);
    sample_free(&second);
    CHECK(!sample_read(&first, "/no/such/folder", NULL));

    char cmd[400];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
    CHECK(system(cmd) == 0);
    return hosttest_done("huginn procs");
}

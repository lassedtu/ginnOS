#pragma once

/**
 * @file procs.h
 * @brief Huginn: a sample of the processes and the machine, from /proc.
 *        plain C (host tested with a /proc made of files).
 *
 * the CPU parts are from two samples: the ticks a process used since the
 * one before, for the time between them.
 */

#include <stdbool.h>
#include <stdint.h>

#define TICKS_PER_SECOND 100 // the kernel's timer (TIMER_HZ)
#define PROC_NAME_MAX    32

typedef struct
{
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    char name[PROC_NAME_MAX];
    char state[16]; // "running", "sleeping" ...
    uint32_t ticks; // CPU time: Utime + Stime
    uint32_t rss_kb;
    double cpu; // percent of the CPU since the sample before (0 in the first)
} proc_t;

typedef struct
{
    proc_t *procs; // by pid
    int count;
    double uptime; // seconds since boot
    double idle;   // of them, seconds the CPU was idle
    uint32_t mem_total_kb;
    uint32_t mem_free_kb;
    double cpu; // percent of the CPU in use since the sample before
} sample_t;

/**
 * read a sample from root ("/proc").
 * @param before the sample before (for the CPU parts), or NULL.
 * @return false when root cannot be read.
 */
bool sample_read(sample_t *s, const char *root, const sample_t *before);
void sample_free(sample_t *s);

/** the process with this pid in a sample, or NULL */
const proc_t *sample_find(const sample_t *s, uint32_t pid);

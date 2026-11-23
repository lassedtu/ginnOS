#pragma once

/**
 * @file huginn.h
 * @brief Huginn, the system monitor: the parts its files share.
 *
 * the files:
 *   main.c       the window, the tabs, a sample each second
 *   procs.c      a sample of /proc (plain C, host tested)
 *   processes.c  the Processes tab: the list, the filter, End process
 *   resources.c  the Resources tab: the CPU graph, the memory, the uptime
 */

#include "procs.h"

#include <apps.h>
#include <idunn.h>

#define HISTORY 60 // samples in the CPU graph: a minute

typedef struct
{
    idunn_app_t *app;
    idunn_window_t *win;
    app_list_t *apps; // for the pictures of the processes
    sample_t now;     // the last sample
    double cpu[HISTORY]; // the CPU in use, the oldest first
    int cpu_count;
} huginn_t;

extern huginn_t H;

/** "4.2 MiB" */
void format_kb(uint32_t kb, char *out, size_t length);

// processes.c
idunn_widget_t *processes_page(void);
void processes_update(void);
/** the window's keys for the list (Delete ends the chosen process) */
bool processes_key(uint16_t code, uint32_t mods);

// resources.c
idunn_widget_t *resources_page(void);
void resources_update(void);

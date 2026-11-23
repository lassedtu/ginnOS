/**
 * @file main.c
 * @brief Huginn, the system monitor: the window, the tabs, and a sample
 *        of /proc each second. (Huginn is Odin's raven that watches the
 *        world.)
 *
 * usage: huginn
 */

#include "huginn.h"

#include <heimdall.h>
#include <stdio.h>
#include <string.h>

#define SAMPLE_MS 1000

huginn_t H;

void format_kb(uint32_t kb, char *out, size_t length)
{
    if (kb < 1024)
        snprintf(out, length, "%u KiB", kb);
    else if (kb < 1024 * 1024)
        snprintf(out, length, "%.1f MiB", kb / 1024.0);
    else
        snprintf(out, length, "%.2f GiB", kb / (1024.0 * 1024.0));
}

static void sample(void *data)
{
    (void)data;
    sample_t fresh;
    if (!sample_read(&fresh, "/proc", H.now.count ? &H.now : NULL))
        return;
    bool first = H.now.uptime == 0;
    sample_free(&H.now);
    H.now = fresh;
    if (!first)
    {
        if (H.cpu_count == HISTORY)
            memmove(H.cpu, H.cpu + 1, (HISTORY - 1) * sizeof(double));
        else
            H.cpu_count++;
        H.cpu[H.cpu_count - 1] = H.now.cpu;
    }
    processes_update();
    resources_update();
}

static bool window_key(idunn_window_t *win, uint16_t code, uint32_t mods, const char *text,
                       void *data)
{
    (void)win;
    (void)text;
    (void)data;
    return processes_key(code, mods);
}

int main(void)
{
    H.app = idunn_app_new("huginn");
    if (!H.app)
    {
        printf("huginn: no window server (start the desktop with startde)\n");
        return 1;
    }
    H.apps = apps_load(APPS_DIR);
    H.win = idunn_window_new(H.app, "Huginn", 760, 560);
    if (!H.win)
        return 1;

    idunn_widget_t *root = idunn_column(0, 0);
    idunn_box_set_padding(root, 2, 3);
    idunn_widget_t *tabs = idunn_grow(idunn_add(root, idunn_tabs(NULL, NULL)), 1);
    idunn_tabs_add(tabs, "Processes", processes_page());
    idunn_tabs_add(tabs, "Resources", resources_page());
    idunn_window_set_root(H.win, root);
    idunn_window_on_key(H.win, window_key, NULL);

    sample(NULL);
    idunn_app_every(H.app, SAMPLE_MS, sample, NULL);
    int status = idunn_app_run(H.app);
    sample_free(&H.now);
    apps_free(H.apps);
    return status;
}

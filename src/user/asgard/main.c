/**
 * @file main.c
 * @brief the desktop shell: the panel at the bottom of the screen.
 *
 * left, the launcher. the middle: a button for each window. right: the
 * keyboard layout, the volume and the clock.
 *
 * asgard is heimdalld's shell (heim_shell_start): it hears of every
 * window and can activate, minimize and close any of them. start it with
 * startde, or as "heimdalld asgard".
 */

#include "asgard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

asgard_t A;

void launch_exec(const char *exec)
{
    char buffer[256];
    char *argv[APP_ARGS_MAX + 1];
    if (strlen(exec) >= sizeof(buffer) || app_split_exec(exec, buffer, argv) <= 0)
        return;
    pid_t child = fork();
    if (child == 0)
    {
        execvp(argv[0], argv);
        _exit(127);
    }
}

static void on_event(idunn_app_t *app, const heim_msg_t *msg, void *data)
{
    (void)app;
    (void)data;
    switch (msg->type)
    {
    case HEIM_EV_WINDOW:
        tasks_window(msg);
        break;
    case HEIM_EV_WINDOW_APP:
        tasks_app(msg);
        break;
    case HEIM_EV_WINDOW_GONE:
        tasks_gone(msg);
        break;
    default:
        break;
    }
}

/**
 * once a second: the clock, the programs of the launcher that ended, and
 * the width of the window buttons (the screen size may have changed).
 */
static void tick(void *data)
{
    (void)data;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }
    clock_tick();
    tasks_fit();
}

static idunn_widget_t *panel_content(void)
{
    idunn_widget_t *root = idunn_column(0, 0);
    idunn_box_set_background(root, IDUNN_COLOR_SIDEBAR);
    idunn_add(root, idunn_separator());
    // gaps and paddings in units of 4 pixels
    idunn_widget_t *row = idunn_grow(idunn_add(root, idunn_row(1, 0)), 1);
    idunn_box_set_padding(row, 1, 2);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);

    idunn_add(row, launcher_button());
    idunn_widget_t *sep = idunn_add(row, idunn_separator());
    idunn_set_size(sep, 0, 24);

    A.tasks = idunn_grow(idunn_add(row, idunn_row(1, 0)), 1);
    idunn_box_set_align(A.tasks, IDUNN_CENTER, IDUNN_START);

    sep = idunn_add(row, idunn_separator());
    idunn_set_size(sep, 0, 24);
    idunn_add(row, layout_button());
    idunn_add(row, volume_button());
    idunn_add(row, clock_button());
    return root;
}

int main(void)
{
    A.app = idunn_app_new("asgard");
    if (!A.app)
    {
        printf("asgard: no window server (start it with startde)\n");
        return 1;
    }
    A.conn = idunn_app_conn(A.app);
    A.panel = idunn_panel_new(A.app, PANEL_HEIGHT);
    if (!A.panel)
        return 1;
    A.apps = apps_load(APPS_DIR);
    idunn_window_set_root(A.panel, panel_content());

    idunn_app_every(A.app, 1000, tick, NULL);
    idunn_app_on_event(A.app, on_event, NULL);
    heim_shell_start(A.conn);
    return idunn_app_run(A.app);
}

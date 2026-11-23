#pragma once

/**
 * @file asgard.h
 * @brief the desktop shell: the parts its files share.
 *
 * the files:
 *   main.c      the panel, the events of the server, the tick
 *   tasks.c     a button for each window
 *   launcher.c  the programs and Log out
 *   tray.c      the keyboard layout, the volume, the clock and calendar
 */

#include <apps.h>
#include <heimdall.h>
#include <idunn.h>
#include <sys/kbd.h>

#define PANEL_HEIGHT 44 // design pixels
#define LAYOUTS_MAX  16

typedef struct
{
    idunn_app_t *app;
    heim_conn_t *conn;
    idunn_window_t *panel;
    app_list_t *apps; // the .app files, read again when the launcher opens

    idunn_widget_t *tasks; // the row of window buttons

    idunn_widget_t *layout; // the keyboard layout button
    idunn_menu_t *layout_menu;
    char layout_names[LAYOUTS_MAX][KBD_LAYOUT_NAME_MAX];
    int layout_count;

    idunn_widget_t *volume; // the volume button (hidden without /dev/mixer)
    idunn_widget_t *clock;
} asgard_t;

extern asgard_t A;

/** start a program; it is not waited for (the tick reaps it) */
void launch_exec(const char *exec);

// tasks.c
void tasks_window(const heim_msg_t *msg);
void tasks_app(const heim_msg_t *msg);
void tasks_gone(const heim_msg_t *msg);
/** the room for the buttons may have changed (the screen size) */
void tasks_fit(void);

// launcher.c
idunn_widget_t *launcher_button(void);

// recent.c: the programs started last, newest first (asgard.conf)
void recent_load(void);
void recent_used(const char *id);
/** where id is among the recent programs (0: the last one), or -1 */
int recent_rank(const char *id);

// tray.c
idunn_widget_t *layout_button(void);
idunn_widget_t *volume_button(void);
idunn_widget_t *clock_button(void);
/** once a second: the clock shows the new minute */
void clock_tick(void);

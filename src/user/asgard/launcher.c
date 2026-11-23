/**
 * @file launcher.c
 * @brief asgard: the launcher: a search field, the programs of
 *        /usr/share/applications as a grid, and the user's name with
 *        Restart and Log out.
 *
 * the search field has the keyboard when the launcher opens, so typing
 * finds a program at once (by its name, its comment or its command);
 * Enter starts the first one shown. Escape closes the launcher.
 */

#include "asgard.h"

#include <ctype.h>
#include <pwd.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>

#define SHOWN_MAX  64
#define GRID_WIDTH 440 // design pixels: four tiles to a row
#define GRID_ROWS  3   // then it scrolls
#define TILE       92  // a tile's height

static const app_t *shown[SHOWN_MAX]; // the grid's items, by index
static int shown_count;
static idunn_widget_t *grid;
static idunn_widget_t *nothing; // "No programs match"

/** true when text has part in it, with no regard to letter case */
static bool has(const char *text, const char *part)
{
    size_t n = strlen(part);
    for (const char *t = text; *t; t++)
    {
        size_t i = 0;
        while (i < n && t[i] && tolower((unsigned char)t[i]) == tolower((unsigned char)part[i]))
            i++;
        if (i == n)
            return true;
    }
    return n == 0;
}

/** the programs that match the search, in the grid */
static void fill(const char *search)
{
    idunn_list_view_clear(grid);
    shown_count = 0;
    for (int i = 0; A.apps && i < A.apps->count && shown_count < SHOWN_MAX; i++)
    {
        const app_t *app = &A.apps->apps[i];
        if (app->hidden)
            continue;
        if (!has(app->name, search) && !has(app->comment, search) && !has(app->exec, search))
            continue;
        shown[shown_count++] = app;
        idunn_list_view_add(grid, app->icon[0] ? app->icon : "application-x-executable",
                            app->name, NULL);
    }
    idunn_set_hidden(nothing, shown_count > 0);
}

static void start(int item, idunn_widget_t *inside)
{
    if (item < 0 || item >= shown_count)
        return;
    launch_exec(shown[item]->exec);
    idunn_popover_close(inside);
}

static void chosen(idunn_widget_t *list, void *data)
{
    (void)data;
    start(idunn_list_view_current(list), list);
}

static void typed(idunn_widget_t *field, void *data)
{
    (void)data;
    fill(idunn_text_field_text(field));
}

static void submitted(idunn_widget_t *field, void *data)
{
    (void)data;
    start(0, field);
}

static void log_out(idunn_widget_t *button, void *data)
{
    (void)button;
    (void)data;
    heim_shell_quit(A.conn);
}

static void restart(idunn_widget_t *button, void *data)
{
    (void)button;
    (void)data;
    reboot(RB_AUTOBOOT); // the disk is written through: nothing waits to be written
}

static void power_off(idunn_widget_t *button, void *data)
{
    (void)button;
    (void)data;
    reboot(RB_POWER_OFF);
}

static idunn_widget_t *footer(void)
{
    idunn_widget_t *row = idunn_row(1, 0);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
    struct passwd *pw = getpwuid(getuid());
    idunn_widget_t *user = idunn_add(row, idunn_button(pw ? pw->pw_name : "user", IDUNN_GHOST,
                                                       NULL, NULL));
    idunn_button_set_icon(user, "circle-user");
    idunn_add(row, idunn_spacer());
    if (getuid() == 0)
    {
        idunn_widget_t *again = idunn_add(row, idunn_icon_button("rotate-cw", IDUNN_GHOST,
                                                                 restart, NULL));
        idunn_set_tooltip(again, "Restart");
        idunn_widget_t *off = idunn_add(row, idunn_icon_button("power", IDUNN_GHOST, power_off,
                                                               NULL));
        idunn_set_tooltip(off, "Power off");
    }
    idunn_widget_t *out = idunn_add(row, idunn_icon_button("log-out", IDUNN_GHOST, log_out,
                                                           NULL));
    idunn_set_tooltip(out, "Log out");
    return row;
}

static void open_launcher(idunn_widget_t *button, void *data)
{
    (void)data;
    // the programs are read each time: a new .app file shows at once
    apps_free(A.apps);
    A.apps = apps_load(APPS_DIR);

    idunn_widget_t *content = idunn_column(3, 0);
    idunn_set_size(content, GRID_WIDTH, 0);
    idunn_widget_t *search = idunn_add(content, idunn_text_field("Search apps"));
    idunn_text_field_set_icon(search, "search");
    idunn_text_field_on_change(search, typed, NULL);
    idunn_text_field_on_submit(search, submitted, NULL);

    grid = idunn_grid_view();
    idunn_list_view_set_single_click(grid, true);
    idunn_list_view_on_activate(grid, chosen, NULL);
    idunn_widget_t *scroll = idunn_add(content, idunn_scroll_view(grid));
    idunn_set_size(scroll, 0, GRID_ROWS * TILE + 8);
    nothing = idunn_add(content, idunn_label("No matching apps", IDUNN_TEXT_SMALL));
    idunn_label_set_color(nothing, IDUNN_COLOR_MUTED_FOREGROUND);
    fill("");

    idunn_add(content, idunn_separator());
    idunn_add(content, footer());
    idunn_popover(button, content);
    idunn_set_focus(search);
}

idunn_widget_t *launcher_button(void)
{
    idunn_widget_t *b = idunn_icon_button("layout-grid", IDUNN_GHOST, open_launcher, NULL);
    idunn_set_tooltip(b, "Apps");
    return b;
}

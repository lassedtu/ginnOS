/**
 * @file tasks.c
 * @brief asgard: a button for each window, with the program's picture and
 *        the window's title.
 *
 * a click activates the window, or minimizes it when it is the active
 * one. the active window's button is filled and has an accent line; a
 * minimized one is dimmer. a right click gives a menu, a middle click
 * closes the window. the buttons are TASK_WIDTH wide and get narrower
 * when there are too many for the panel.
 */

#include "asgard.h"

#include <stdio.h>
#include <string.h>

#define TASK_WIDTH 200 // design pixels, the most
#define TASK_MIN   44  // the least: the picture only
#define TASK_GAP   4   // the row's gap (1 unit)
#define TASKS_MAX  32

typedef struct
{
    bool used;
    uint32_t id;    // the server's number for the window
    uint32_t state; // HEIM_STATE_
    char title[HEIM_TITLE_MAX];
    char app[HEIM_APP_ID_MAX];
    idunn_widget_t *button;
} task_t;

static task_t tasks[TASKS_MAX];

// the program of a window can come before the window shows: kept here
static struct
{
    uint32_t id;
    char app[HEIM_APP_ID_MAX];
} early[TASKS_MAX];
static int early_next;
static task_t *menu_task; // the task the menu is for

static task_t *find(uint32_t id)
{
    for (int i = 0; i < TASKS_MAX; i++)
        if (tasks[i].used && tasks[i].id == id)
            return &tasks[i];
    return NULL;
}

static bool on_top(const task_t *t)
{
    return (t->state & HEIM_STATE_FOCUSED) && !(t->state & HEIM_STATE_MINIMIZED);
}

static void clicked(idunn_widget_t *button, void *data)
{
    (void)button;
    task_t *t = data;
    heim_shell_act(A.conn, t->id, on_top(t) ? HEIM_ACT_MINIMIZE : HEIM_ACT_ACTIVATE);
}

static void middle_clicked(idunn_widget_t *button, void *data)
{
    (void)button;
    task_t *t = data;
    heim_shell_act(A.conn, t->id, HEIM_ACT_CLOSE);
}

static void menu_chosen(idunn_widget_t *owner, void *data)
{
    (void)owner;
    if (menu_task && menu_task->used)
        heim_shell_act(A.conn, menu_task->id, (uint32_t)(uintptr_t)data);
}

/** a window's menu: its first item, then Close */
static idunn_menu_t *make_menu(const char *first, uint32_t action)
{
    idunn_menu_t *m = idunn_menu_new();
    idunn_menu_add(m, NULL, first, NULL, menu_chosen, (void *)(uintptr_t)action);
    idunn_menu_add_separator(m);
    idunn_menu_add(m, "x", "Close", NULL, menu_chosen, (void *)(uintptr_t)HEIM_ACT_CLOSE);
    return m;
}

static void menu_opened(idunn_widget_t *button, void *data)
{
    // made once: a menu is not freed while it may be open
    static idunn_menu_t *minimize, *restore, *front;
    if (!minimize)
    {
        minimize = make_menu("Minimize", HEIM_ACT_MINIMIZE);
        restore = make_menu("Restore", HEIM_ACT_ACTIVATE);
        front = make_menu("Bring to front", HEIM_ACT_ACTIVATE);
    }
    task_t *t = data;
    menu_task = t;
    idunn_menu_t *m = on_top(t) ? minimize : (t->state & HEIM_STATE_MINIMIZED) ? restore : front;
    idunn_menu_popup_at_pointer(m, button);
}

/** the picture of the window's program (from its .app file) */
static const char *picture_of(const task_t *t)
{
    const app_t *app = t->app[0] && A.apps ? apps_find(A.apps, t->app) : NULL;
    return app && app->icon[0] ? app->icon : "application-x-executable";
}

static void show(task_t *t)
{
    const char *text = t->title[0] ? t->title : "Window";
    idunn_button_set_text(t->button, text);
    idunn_button_set_picture(t->button, picture_of(t));
    idunn_button_set_active(t->button, on_top(t));
    idunn_button_set_dim(t->button, (t->state & HEIM_STATE_MINIMIZED) != 0);
    idunn_set_tooltip(t->button, text);
}

void tasks_fit(void)
{
    int count = 0;
    for (int i = 0; i < TASKS_MAX; i++)
        count += tasks[i].used;
    if (count == 0)
        return;
    int32_t scale = idunn_app_theme(A.app)->scale;
    int32_t room = idunn_widget_rect(A.tasks).w / (scale ? scale : 1);
    int32_t width = room > 0 ? (room - TASK_GAP * (count - 1)) / count : TASK_WIDTH;
    if (width > TASK_WIDTH)
        width = TASK_WIDTH;
    if (width < TASK_MIN)
        width = TASK_MIN;
    static int32_t last_width, last_count;
    if (width == last_width && count == last_count)
        return;
    last_width = width;
    last_count = count;
    for (int i = 0; i < TASKS_MAX; i++)
        if (tasks[i].used)
            idunn_set_size(tasks[i].button, width, 0);
}

void tasks_window(const heim_msg_t *msg)
{
    task_t *t = find(msg->info.id);
    if (!t)
    {
        for (int i = 0; i < TASKS_MAX && !t; i++)
            if (!tasks[i].used)
                t = &tasks[i];
        if (!t)
            return; // more windows than buttons
        memset(t, 0, sizeof(*t));
        t->used = true;
        t->id = msg->info.id;
        t->button = idunn_add(A.tasks, idunn_button("", IDUNN_GHOST, clicked, t));
        idunn_button_set_align(t->button, IDUNN_START);
        idunn_on_middle_click(t->button, middle_clicked, t);
        idunn_on_menu(t->button, menu_opened, t);
        for (int i = 0; i < TASKS_MAX; i++)
            if (early[i].id == t->id && early[i].app[0])
            {
                memcpy(t->app, early[i].app, HEIM_APP_ID_MAX);
                early[i].app[0] = '\0';
            }
        tasks_fit();
    }
    t->state = msg->info.state;
    memcpy(t->title, msg->info.title, HEIM_TITLE_MAX);
    t->title[HEIM_TITLE_MAX - 1] = '\0';
    show(t);
}

void tasks_app(const heim_msg_t *msg)
{
    task_t *t = find(msg->app.id);
    if (!t)
    {
        early[early_next].id = msg->app.id;
        snprintf(early[early_next].app, HEIM_APP_ID_MAX, "%.*s", HEIM_APP_ID_MAX - 1,
                 msg->app.app);
        early_next = (early_next + 1) % TASKS_MAX;
        return;
    }
    snprintf(t->app, sizeof(t->app), "%.*s", HEIM_APP_ID_MAX - 1, msg->app.app);
    if (!A.apps)
        A.apps = apps_load(APPS_DIR);
    show(t);
}

void tasks_gone(const heim_msg_t *msg)
{
    task_t *t = find(msg->info.id);
    if (!t)
        return;
    if (menu_task == t)
        menu_task = NULL;
    idunn_widget_free(t->button);
    t->used = false;
    tasks_fit();
}

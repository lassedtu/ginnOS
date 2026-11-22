/**
 * @file asgard.c
 * @brief the desktop shell: the panel at the bottom of the screen.
 *
 * left, the launcher: the programs of /usr/share/applications and Log
 * out. the middle: a button for each window (a click activates it, or
 * minimizes it when it has the keyboard). right: the keyboard layout,
 * the volume and the clock.
 *
 * asgard is heimdalld's shell (heim_shell_start): it hears of every
 * window and can activate and minimize any of them. start it with
 * startde, or as "heimdalld asgard".
 */

#include <apps.h>
#include <conf.h>
#include <fcntl.h>
#include <heimdall.h>
#include <idunn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/kbd.h>
#include <sys/soundcard.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PANEL_HEIGHT 44  // design pixels
#define TASK_WIDTH   180 // a window's button
#define TASKS_MAX    32
#define LAYOUTS_MAX  16

typedef struct
{
    bool used;
    uint32_t id;    // the server's number for the window
    uint32_t state; // HEIM_STATE_
    char title[HEIM_TITLE_MAX];
    idunn_widget_t *button;
} task_t;

static struct
{
    idunn_app_t *app;
    heim_conn_t *conn;
    idunn_window_t *panel;

    app_list_t *apps;
    const app_t *shown[64]; // the launcher's items, by index
    int shown_count;

    idunn_widget_t *tasks; // the row of window buttons
    task_t task[TASKS_MAX];

    idunn_widget_t *layout; // the keyboard layout button
    idunn_menu_t *layout_menu;
    char layout_names[LAYOUTS_MAX][KBD_LAYOUT_NAME_MAX];
    int layout_count;

    idunn_widget_t *volume; // the volume button (hidden without /dev/mixer)
    idunn_widget_t *volume_slider;

    idunn_widget_t *clock;
    int clock_minute; // the minute the clock shows (-1: none yet)
} A;

/**
 * start a program of the launcher; it is not waited for (tick reaps it).
 */
static void launch(const app_t *app)
{
    char buffer[256];
    char *argv[APP_ARGS_MAX + 1];
    if (strlen(app->exec) >= sizeof(buffer) || app_split_exec(app->exec, buffer, argv) <= 0)
        return;
    pid_t child = fork();
    if (child == 0)
    {
        execvp(argv[0], argv);
        _exit(127);
    }
}

static void launcher_chosen(idunn_widget_t *grid, void *data)
{
    (void)data;
    int item = idunn_list_view_current(grid);
    if (item >= 0 && item < A.shown_count)
        launch(A.shown[item]);
    idunn_popover_close(grid);
}

static void log_out(idunn_widget_t *button, void *data)
{
    (void)button;
    (void)data;
    heim_shell_quit(A.conn);
}

static void open_launcher(idunn_widget_t *button, void *data)
{
    (void)data;
    // the programs are read each time: a new .app file shows at once
    apps_free(A.apps);
    A.apps = apps_load(APPS_DIR);
    A.shown_count = 0;

    idunn_widget_t *content = idunn_column(2, 0);
    idunn_add(content, idunn_label("Programs", IDUNN_TEXT_LABEL));
    idunn_widget_t *grid = idunn_grid_view();
    for (int i = 0; A.apps && i < A.apps->count; i++)
    {
        const app_t *app = &A.apps->apps[i];
        if (app->hidden || A.shown_count == (int)(sizeof(A.shown) / sizeof(A.shown[0])))
            continue;
        A.shown[A.shown_count++] = app;
        idunn_list_view_add(grid, app->icon[0] ? app->icon : "application-x-executable",
                            app->name, NULL);
    }
    if (A.shown_count == 0)
        idunn_add(content, idunn_label("No programs in " APPS_DIR, IDUNN_TEXT_SMALL));
    idunn_list_view_on_select(grid, launcher_chosen, NULL);
    // as high as its tiles (two to a row), up to three rows; then it scrolls
    if (A.shown_count > 6)
        idunn_set_size(grid, 0, 3 * 92 + 8);
    idunn_add(content, grid);
    idunn_add(content, idunn_separator());
    idunn_widget_t *out = idunn_add(content, idunn_button("Log out", IDUNN_GHOST, log_out, NULL));
    idunn_button_set_icon(out, "log-out");
    idunn_button_set_align(out, IDUNN_START);
    idunn_popover(button, content);
}

static task_t *find_task(uint32_t id)
{
    for (int i = 0; i < TASKS_MAX; i++)
        if (A.task[i].used && A.task[i].id == id)
            return &A.task[i];
    return NULL;
}

static void task_clicked(idunn_widget_t *button, void *data)
{
    (void)button;
    task_t *t = data;
    bool on_top = (t->state & HEIM_STATE_FOCUSED) && !(t->state & HEIM_STATE_MINIMIZED);
    heim_shell_act(A.conn, t->id, on_top ? HEIM_ACT_MINIMIZE : HEIM_ACT_ACTIVATE);
}

/**
 * show a window's button as its state is: marked when it has the keyboard.
 */
static void show_task(task_t *t)
{
    const char *text = t->title[0] ? t->title : "Window";
    idunn_button_set_text(t->button, text);
    bool focused = (t->state & HEIM_STATE_FOCUSED) && !(t->state & HEIM_STATE_MINIMIZED);
    idunn_button_set_variant(t->button, focused ? IDUNN_SECONDARY : IDUNN_GHOST);
    idunn_set_tooltip(t->button, text);
}

static void window_changed(const heim_msg_t *msg)
{
    task_t *t = find_task(msg->info.id);
    if (!t)
    {
        for (int i = 0; i < TASKS_MAX && !t; i++)
            if (!A.task[i].used)
                t = &A.task[i];
        if (!t)
            return; // more windows than buttons
        memset(t, 0, sizeof(*t));
        t->used = true;
        t->id = msg->info.id;
        t->button = idunn_add(A.tasks, idunn_button("", IDUNN_GHOST, task_clicked, t));
        idunn_button_set_align(t->button, IDUNN_START);
        idunn_set_size(t->button, TASK_WIDTH, 0);
    }
    t->state = msg->info.state;
    memcpy(t->title, msg->info.title, HEIM_TITLE_MAX);
    t->title[HEIM_TITLE_MAX - 1] = '\0';
    show_task(t);
}

static void window_gone(const heim_msg_t *msg)
{
    task_t *t = find_task(msg->info.id);
    if (!t)
        return;
    idunn_widget_free(t->button);
    t->used = false;
}

static void on_event(idunn_app_t *app, const heim_msg_t *msg, void *data)
{
    (void)app;
    (void)data;
    switch (msg->type)
    {
    case HEIM_EV_WINDOW:
        window_changed(msg);
        break;
    case HEIM_EV_WINDOW_GONE:
        window_gone(msg);
        break;
    default:
        break; // HEIM_EV_WINDOW_APP: the icons come later
    }
}

/**
 * the short name of a layout for the panel: "dk-mac" shows as "DK".
 */
static void layout_short(const char *name, char out[4])
{
    int n = 0;
    while (n < 3 && name[n] && name[n] != '-' && name[n] != '_')
    {
        out[n] = (char)(name[n] >= 'a' && name[n] <= 'z' ? name[n] - 'a' + 'A' : name[n]);
        n++;
    }
    out[n] = '\0';
}

static void layout_chosen(idunn_widget_t *owner, void *data)
{
    (void)owner;
    uint32_t index = (uint32_t)(uintptr_t)data;
    int kbd = open("/dev/kbd", O_RDONLY | O_CLOEXEC);
    if (kbd < 0)
        return;
    if (ioctl(kbd, KBD_IOC_SET_LAYOUT, &index) == 0)
    {
        char text[4];
        layout_short(A.layout_names[index], text);
        idunn_button_set_text(A.layout, text);
        for (int i = 0; i < A.layout_count; i++)
            idunn_menu_set_checked(A.layout_menu, i, (uint32_t)i == index);
        // kept for the next session (heimdalld makes it active at the start)
        conf_t *conf = conf_open("keyboard");
        if (conf)
        {
            conf_set(conf, "keyboard", "layout", A.layout_names[index]);
            conf_save(conf);
            conf_free(conf);
        }
        heim_settings_changed(A.conn, "keyboard"); // heimdalld reads it again
    }
    close(kbd);
}

static void open_layouts(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_menu_popup(A.layout_menu, button);
}

/**
 * the keyboard layouts the kernel has; the button shows the active one.
 */
static idunn_widget_t *layout_button(void)
{
    A.layout_menu = idunn_menu_new();
    A.layout = idunn_button("", IDUNN_GHOST, open_layouts, NULL);
    idunn_button_set_icon(A.layout, "keyboard");
    int kbd = open("/dev/kbd", O_RDONLY | O_CLOEXEC);
    uint32_t count = 0;
    uint32_t active = 0;
    if (kbd < 0 || ioctl(kbd, KBD_IOC_LAYOUT_COUNT, &count) != 0 ||
        ioctl(kbd, KBD_IOC_GET_LAYOUT, &active) != 0)
        count = 0;
    for (uint32_t i = 0; i < count && i < LAYOUTS_MAX; i++)
    {
        struct kbd_layout_name name = {.index = i};
        if (ioctl(kbd, KBD_IOC_LAYOUT_NAME, &name) != 0)
            break;
        snprintf(A.layout_names[i], KBD_LAYOUT_NAME_MAX, "%s", name.name);
        idunn_menu_add_check(A.layout_menu, A.layout_names[i], i == active, layout_chosen,
                             (void *)(uintptr_t)i);
        A.layout_count++;
    }
    if (kbd >= 0)
        close(kbd);
    if (A.layout_count == 0)
        idunn_set_hidden(A.layout, true);
    else
    {
        char text[4];
        layout_short(A.layout_names[active < (uint32_t)A.layout_count ? active : 0], text);
        idunn_button_set_text(A.layout, text);
    }
    return A.layout;
}

/**
 * the volume of /dev/mixer, 0 to 100 (both channels the same), or -1.
 */
static int read_volume(void)
{
    int mixer = open("/dev/mixer", O_RDONLY | O_CLOEXEC);
    int value = -1;
    if (mixer >= 0 && ioctl(mixer, SOUND_MIXER_READ_VOLUME, &value) != 0)
        value = -1;
    if (mixer >= 0)
        close(mixer);
    return value < 0 ? -1 : value & 0xFF;
}

static void volume_changed(idunn_widget_t *slider, void *data)
{
    (void)data;
    int level = idunn_slider_value(slider);
    int value = level | (level << 8); // left and right
    int mixer = open("/dev/mixer", O_RDWR | O_CLOEXEC);
    if (mixer >= 0)
    {
        ioctl(mixer, SOUND_MIXER_WRITE_VOLUME, &value);
        close(mixer);
    }
    idunn_button_set_icon(A.volume, level == 0 ? "volume-x" : "volume-2");
}

static void open_volume(idunn_widget_t *button, void *data)
{
    (void)data;
    int level = read_volume();
    idunn_widget_t *content = idunn_column(2, 0);
    idunn_add(content, idunn_label("Volume", IDUNN_TEXT_LABEL));
    A.volume_slider = idunn_add(content, idunn_slider(0, 100, 5, level < 0 ? 0 : level,
                                                      volume_changed, NULL));
    idunn_popover(button, content);
}

static void open_calendar(idunn_widget_t *button, void *data)
{
    (void)data;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char day[64];
    char date[64];
    strftime(day, sizeof(day), "%A", &tm);
    strftime(date, sizeof(date), "%d %B %Y", &tm);
    idunn_widget_t *content = idunn_column(1, 0);
    idunn_add(content, idunn_label(day, IDUNN_TEXT_LABEL));
    idunn_add(content, idunn_label(date, IDUNN_TEXT_BODY));
    idunn_popover(button, content);
}

/**
 * once a second: the clock when the minute changed, and the programs of
 * the launcher that ended.
 */
static void tick(void *data)
{
    (void)data;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_min == A.clock_minute)
        return;
    A.clock_minute = tm.tm_min;
    char text[8];
    strftime(text, sizeof(text), "%H:%M", &tm);
    idunn_button_set_text(A.clock, text);
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

    idunn_widget_t *launcher =
        idunn_add(row, idunn_icon_button("layout-grid", IDUNN_GHOST, open_launcher, NULL));
    idunn_set_tooltip(launcher, "Programs");
    idunn_add(row, idunn_separator());

    A.tasks = idunn_grow(idunn_add(row, idunn_row(1, 0)), 1);
    idunn_box_set_align(A.tasks, IDUNN_CENTER, IDUNN_START);

    idunn_add(row, layout_button());
    A.volume = idunn_add(row, idunn_icon_button("volume-2", IDUNN_GHOST, open_volume, NULL));
    idunn_set_tooltip(A.volume, "Volume");
    int level = read_volume();
    if (level < 0)
        idunn_set_hidden(A.volume, true);
    else if (level == 0)
        idunn_button_set_icon(A.volume, "volume-x");
    A.clock = idunn_add(row, idunn_button("", IDUNN_GHOST, open_calendar, NULL));
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
    idunn_window_set_root(A.panel, panel_content());

    A.clock_minute = -1;
    tick(NULL);
    idunn_app_every(A.app, 1000, tick, NULL);
    idunn_app_on_event(A.app, on_event, NULL);
    heim_shell_start(A.conn);
    return idunn_app_run(A.app);
}

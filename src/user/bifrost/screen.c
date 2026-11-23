/**
 * @file screen.c
 * @brief Bifrost: the login screen.
 *
 * the wallpaper, blurred and darkened, and on the left a column: the date
 * over a very large clock; the user and the password in see-through
 * pills; Log in; Power off and Restart as tiles; the system's name at the
 * bottom. the user pill opens a menu of the users when there are more.
 * Enter in the password logs in. a wrong password says so, empties the
 * field, and Log in waits a second before the next try. the screen is
 * always dark: its text is over the darkened wallpaper.
 */

#include "bifrost.h"

#include <crypt.h>
#include <heimdall.h>
#include <idunn.h>
#include <shadow.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define COLUMN_WIDTH 360 // design pixels
#define WAIT_MS      1000
#define TICK_MS      250

static struct
{
    idunn_app_t *app;
    login_user_t users[USERS_MAX];
    int count;
    int chosen;
    idunn_menu_t *menu; // the users, when there are more than one
    idunn_widget_t *time;
    idunn_widget_t *date;
    idunn_widget_t *who; // the user pill
    idunn_widget_t *password;
    idunn_widget_t *error;
    idunn_widget_t *log_in;
    uint32_t wait; // milliseconds until Log in works again
    bool done;
    char *name_out;
    size_t name_length;
} B;

static void show_time(void *data)
{
    (void)data;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char text[64];
    strftime(text, sizeof(text), "%H:%M", &tm);
    idunn_label_set_text(B.time, text);
    strftime(text, sizeof(text), "%A, %d %B", &tm);
    idunn_label_set_text(B.date, text[0] ? text : " ");
}

/** the second after a wrong password */
static void tick(void *data)
{
    (void)data;
    if (B.wait == 0)
        return;
    B.wait = B.wait > TICK_MS ? B.wait - TICK_MS : 0;
    if (B.wait == 0)
        idunn_set_disabled(B.log_in, false);
}

/** true when the password of the chosen user is right */
static bool password_ok(const char *typed)
{
    struct spwd *sp = getspnam(B.users[B.chosen].name);
    if (!sp)
        return false;
    char hash[160];
    snprintf(hash, sizeof(hash), "%s", sp->sp_pwdp);
    if (hash[0] == '\0')
        return true; // an account with no password
    return hash[0] != '!' && hash[0] != '*' && crypt_verify(typed, hash);
}

static void try_log_in(void)
{
    if (B.wait)
        return;
    const char *typed = idunn_text_field_text(B.password);
    if (password_ok(typed))
    {
        snprintf(B.name_out, B.name_length, "%s", B.users[B.chosen].name);
        B.done = true;
        idunn_app_quit(B.app);
        return;
    }
    idunn_label_set_text(B.error, "Incorrect password. Try again.");
    idunn_set_hidden(B.error, false);
    idunn_set_error(B.password, true);
    idunn_text_field_set_text(B.password, "");
    idunn_set_focus(B.password);
    idunn_set_disabled(B.log_in, true);
    B.wait = WAIT_MS;
}

static void log_in_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    try_log_in();
}

static void submitted(idunn_widget_t *field, void *data)
{
    (void)field;
    (void)data;
    try_log_in();
}

static void typed(idunn_widget_t *field, void *data)
{
    (void)data;
    if (idunn_text_field_text(field)[0])
    {
        idunn_set_error(field, false);
        idunn_set_hidden(B.error, true);
    }
}

static void show_user(void)
{
    idunn_button_set_text(B.who, B.users[B.chosen].full_name);
    for (int i = 0; B.menu && i < B.count; i++)
        idunn_menu_set_checked(B.menu, i, i == B.chosen);
    idunn_text_field_set_text(B.password, "");
    idunn_set_error(B.password, false);
    idunn_set_hidden(B.error, true);
}

static void user_chosen(idunn_widget_t *w, void *data)
{
    (void)w;
    B.chosen = (int)(intptr_t)data;
    show_user();
    idunn_set_focus(B.password);
}

static void who_clicked(idunn_widget_t *b, void *data)
{
    (void)data;
    if (B.menu)
        idunn_menu_popup(B.menu, b);
}

static void restart(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    reboot(RB_AUTOBOOT);
}

static void power_off(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    reboot(RB_POWER_OFF);
}

/** a gap of this many design pixels in a column */
static void gap(idunn_widget_t *column, int32_t height)
{
    idunn_widget_t *g = idunn_add(column, idunn_row(0, 0));
    idunn_set_size(g, 0, height);
}

static idunn_widget_t *tile(const char *icon, const char *text, idunn_callback_t on_click)
{
    idunn_widget_t *b = idunn_button(text, IDUNN_GHOST, on_click, NULL);
    idunn_button_set_icon(b, icon);
    idunn_button_set_tile(b, true);
    return b;
}

/** the column on the left: the clock, the form, the power tiles */
static idunn_widget_t *column(void)
{
    idunn_widget_t *c = idunn_column(0, 0);
    idunn_set_size(c, COLUMN_WIDTH, 0);
    idunn_box_set_align(c, IDUNN_STRETCH, IDUNN_START);

    idunn_grow(idunn_add(c, idunn_spacer()), 2);
    idunn_widget_t *clock = idunn_add(c, idunn_column(0, 0));
    idunn_box_set_align(clock, IDUNN_CENTER, IDUNN_START);
    B.date = idunn_add(clock, idunn_label(" ", IDUNN_TEXT_HEADING));
    B.time = idunn_add(clock, idunn_label(" ", IDUNN_TEXT_HERO));
    gap(c, 56);

    B.who = idunn_add(c, idunn_button(" ", IDUNN_GHOST, who_clicked, NULL));
    idunn_button_set_size(B.who, IDUNN_SIZE_LARGE);
    idunn_button_set_pill(B.who, true);
    idunn_button_set_glass(B.who, true);
    if (B.count > 1)
        idunn_button_set_icon(B.who, "chevrons-up-down");
    gap(c, 12);
    B.password = idunn_add(c, idunn_text_field("Password"));
    idunn_text_field_set_pill(B.password, true);
    idunn_text_field_set_secret(B.password, true);
    idunn_text_field_on_submit(B.password, submitted, NULL);
    idunn_text_field_on_change(B.password, typed, NULL);
    gap(c, 8);
    idunn_widget_t *error_row = idunn_add(c, idunn_row(0, 0));
    idunn_box_set_align(error_row, IDUNN_CENTER, IDUNN_CENTER);
    B.error = idunn_add(error_row, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_label_set_color(B.error, IDUNN_COLOR_DESTRUCTIVE);
    idunn_set_hidden(B.error, true);
    gap(c, 16);
    B.log_in = idunn_add(c, idunn_button("Log in", IDUNN_PRIMARY, log_in_clicked, NULL));
    idunn_button_set_size(B.log_in, IDUNN_SIZE_LARGE);
    idunn_button_set_pill(B.log_in, true);

    idunn_grow(idunn_add(c, idunn_spacer()), 1);
    idunn_widget_t *power = idunn_add(c, idunn_row(4, 0));
    idunn_box_set_align(power, IDUNN_CENTER, IDUNN_CENTER);
    idunn_add(power, tile("power", "Power off", power_off));
    idunn_add(power, tile("rotate-ccw", "Restart", restart));
    gap(c, 40);

    struct utsname u;
    char about[sizeof(u.sysname) + sizeof(u.release) + 2];
    snprintf(about, sizeof(about), "%s", "ginnOS");
    if (uname(&u) == 0)
        snprintf(about, sizeof(about), "%s %s", u.sysname, u.release);
    idunn_widget_t *foot = idunn_add(c, idunn_row(0, 0));
    idunn_box_set_align(foot, IDUNN_CENTER, IDUNN_CENTER);
    idunn_widget_t *name = idunn_add(foot, idunn_label(about, IDUNN_TEXT_SMALL));
    idunn_label_set_color(name, IDUNN_COLOR_MUTED_FOREGROUND);
    return c;
}

static void make_menu(void)
{
    if (B.count < 2)
        return;
    B.menu = idunn_menu_new();
    if (!B.menu)
        return;
    for (int i = 0; i < B.count; i++)
        idunn_menu_add_check(B.menu, B.users[i].full_name, i == B.chosen, user_chosen,
                             (void *)(intptr_t)i);
    idunn_menu_set_min_width(B.menu, COLUMN_WIDTH);
}

bool screen_run(const char *last, char *name_out, size_t length)
{
    memset(&B, 0, sizeof(B));
    B.name_out = name_out;
    B.name_length = length;
    B.count = users_read(B.users, USERS_MAX);
    if (B.count == 0)
        return false;
    // the user who logged in last, else the first who is not root
    B.chosen = 0;
    for (int i = B.count - 1; i >= 0; i--)
        if (B.users[i].uid != 0)
            B.chosen = i;
    for (int i = 0; i < B.count; i++)
        if (last[0] && strcmp(B.users[i].name, last) == 0)
            B.chosen = i;

    B.app = idunn_app_new("bifrost");
    if (!B.app)
        return false;
    idunn_theme_t theme = *idunn_app_theme(B.app);
    theme.mode = IDUNN_DARK;
    idunn_app_set_theme(B.app, &theme);
    int32_t width;
    int32_t height;
    heim_screen_size(idunn_app_conn(B.app), &width, &height);
    gfx_buffer_t *backdrop = backdrop_make(width, height);
    idunn_window_t *win = idunn_screen_window_new(B.app, backdrop);
    if (!win)
    {
        gfx_buffer_free(backdrop);
        return false;
    }

    // the column a fifth of the way across, as tall as the screen
    idunn_widget_t *root = idunn_row(0, 0);
    idunn_box_set_padding(root, 10, 0);
    idunn_box_set_align(root, IDUNN_STRETCH, IDUNN_START);
    idunn_grow(idunn_add(root, idunn_spacer()), 1);
    idunn_add(root, column());
    idunn_grow(idunn_add(root, idunn_spacer()), 4);
    idunn_window_set_root(win, root);
    make_menu();

    show_time(NULL);
    show_user();
    idunn_app_every(B.app, 1000, show_time, NULL);
    idunn_app_every(B.app, TICK_MS, tick, NULL);
    idunn_set_focus(B.password);
    idunn_app_run(B.app); // frees the app
    idunn_menu_free(B.menu);
    gfx_buffer_free(backdrop);
    return B.done;
}

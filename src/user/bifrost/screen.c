/**
 * @file screen.c
 * @brief Bifrost: the login screen.
 *
 * the wallpaper, blurred and darkened; the time and the date at the top;
 * a card of frosted glass in the middle with the user, the password and
 * Log in; a button for each user under it when there are more; Restart
 * and Power off at the bottom. Enter in the password logs in. a wrong
 * password says so, empties the field, and Log in waits a second before
 * the next try. the screen is always dark: its text is over the darkened
 * wallpaper.
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
#include <time.h>
#include <unistd.h>

#define CARD_WIDTH 340 // design pixels
#define WAIT_MS    1000

static struct
{
    idunn_app_t *app;
    login_user_t users[USERS_MAX];
    int count;
    int chosen;
    idunn_widget_t *time;
    idunn_widget_t *date;
    idunn_widget_t *who;      // the user's full name
    idunn_widget_t *user_buttons[USERS_MAX]; // under the card, when there are more users
    idunn_widget_t *password;
    idunn_widget_t *error;
    idunn_widget_t *log_in;
    uint32_t wait;            // milliseconds until Log in works again
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
    strftime(text, sizeof(text), "%A %d %B", &tm);
    idunn_label_set_text(B.date, text);
}

/** the second after a wrong password */
static void tick(void *data)
{
    (void)data;
    if (B.wait == 0)
        return;
    B.wait = B.wait > 250 ? B.wait - 250 : 0;
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
    idunn_label_set_text(B.who, B.users[B.chosen].full_name);
    for (int i = 0; i < B.count; i++)
        if (B.user_buttons[i])
            idunn_button_set_active(B.user_buttons[i], i == B.chosen);
    idunn_text_field_set_text(B.password, "");
    idunn_set_error(B.password, false);
    idunn_set_hidden(B.error, true);
}

static void user_chosen(idunn_widget_t *button, void *data)
{
    (void)button;
    B.chosen = (int)(intptr_t)data;
    show_user();
    idunn_set_focus(B.password);
}

/** a button for each user, under the card */
static idunn_widget_t *user_row(void)
{
    idunn_widget_t *row = idunn_row(2, 0);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_CENTER);
    for (int i = 0; B.count > 1 && i < B.count; i++)
    {
        B.user_buttons[i] = idunn_add(row, idunn_button(B.users[i].full_name, IDUNN_GHOST,
                                                        user_chosen, (void *)(intptr_t)i));
        idunn_button_set_icon(B.user_buttons[i], "circle-user");
    }
    return row;
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

static idunn_widget_t *card(void)
{
    idunn_widget_t *c = idunn_card(3);
    idunn_card_set_glass(c, true);
    idunn_set_size(c, CARD_WIDTH, 0);
    idunn_widget_t *head = idunn_add(c, idunn_column(1, 0));
    idunn_box_set_align(head, IDUNN_CENTER, IDUNN_START);
    idunn_add(head, idunn_icon_view("circle-user", 64, IDUNN_COLOR_FOREGROUND));
    B.who = idunn_add(head, idunn_label(" ", IDUNN_TEXT_TITLE));
    B.password = idunn_add(c, idunn_text_field("Password"));
    idunn_text_field_set_secret(B.password, true);
    idunn_text_field_set_icon(B.password, "lock");
    idunn_text_field_on_submit(B.password, submitted, NULL);
    idunn_text_field_on_change(B.password, typed, NULL);
    B.error = idunn_add(c, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_label_set_color(B.error, IDUNN_COLOR_DESTRUCTIVE);
    idunn_set_hidden(B.error, true);
    B.log_in = idunn_add(c, idunn_button("Log in", IDUNN_PRIMARY, log_in_clicked, NULL));
    return c;
}

static idunn_widget_t *power_row(void)
{
    idunn_widget_t *row = idunn_row(1, 0);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_END);
    idunn_widget_t *again = idunn_add(row, idunn_icon_button("rotate-cw", IDUNN_GHOST, restart,
                                                             NULL));
    idunn_set_tooltip(again, "Restart");
    idunn_widget_t *off = idunn_add(row, idunn_icon_button("power", IDUNN_GHOST, power_off,
                                                           NULL));
    idunn_set_tooltip(off, "Power off");
    return row;
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

    idunn_widget_t *root = idunn_column(0, 0);
    idunn_box_set_padding(root, 6, 6);
    idunn_box_set_align(root, IDUNN_CENTER, IDUNN_START);
    B.time = idunn_add(root, idunn_label(" ", IDUNN_TEXT_DISPLAY));
    B.date = idunn_add(root, idunn_label(" ", IDUNN_TEXT_BODY));
    idunn_label_set_color(B.date, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_grow(idunn_add(root, idunn_spacer()), 1);
    idunn_add(root, card());
    idunn_widget_t *gap = idunn_add(root, idunn_row(0, 0));
    idunn_set_size(gap, 0, 16);
    idunn_add(root, user_row());
    idunn_grow(idunn_add(root, idunn_spacer()), 2);
    idunn_widget_t *bottom = idunn_add(root, power_row());
    idunn_set_size(bottom, 0, 0);
    idunn_window_set_root(win, root);

    show_time(NULL);
    show_user();
    idunn_app_every(B.app, 1000, show_time, NULL);
    idunn_app_every(B.app, 250, tick, NULL);
    idunn_set_focus(B.password);
    idunn_app_run(B.app); // frees the app
    gfx_buffer_free(backdrop);
    return B.done;
}

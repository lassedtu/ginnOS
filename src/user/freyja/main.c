/**
 * @file main.c
 * @brief Freyja, the image viewer: the window, the toolbar, the status
 *        line, the keys, and opening pictures.
 *
 * usage: freyja [picture]
 *
 *   Left, Right (Page Up, Page Down, Space)   the picture before, the next
 *   plus, minus                               zoom in, zoom out
 *   0 (F), 1                                  fit in the window, 100 %
 *   R, Shift+R                                turn right, turn left (not saved)
 *   Ctrl+O, Ctrl+W                            open, close
 */

#include "freyja.h"

#include <heimdall.h>
#include <image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>
#include <unistd.h>

#define ZOOM_STEP 1.25

freyja_t F;

static idunn_widget_t *holder;  // the canvas, or the empty state
static idunn_widget_t *empty;
static idunn_widget_t *path_dialog;
static idunn_widget_t *path_field;

static void open_dialog(void);

static void clicked_open(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    open_dialog();
}

/** the empty state in place of the canvas: nothing open, or an error */
static void show_empty(void)
{
    idunn_widget_free(empty);
    empty = idunn_empty_state(F.error[0] ? "circle-alert" : "image",
                              F.error[0] ? "Cannot show the picture" : "No picture",
                              F.error[0] ? F.error : "Open a picture, or open one in Yggdrasil.");
    idunn_widget_t *open = idunn_add(empty, idunn_button("Open...", IDUNN_OUTLINE, clicked_open,
                                                         NULL));
    (void)open;
    idunn_grow(idunn_add(holder, empty), 1);
    idunn_set_hidden(F.canvas, true);
}

void freyja_changed(void)
{
    char text[PATH_LEN + 64];
    const char *name = F.path[0] ? strrchr(F.path, '/') + 1 : "Freyja";
    idunn_window_set_title(F.win, name);
    if (!F.picture.level_count)
    {
        idunn_label_set_text(F.zoom_label, " ");
        idunn_label_set_text(F.status_name, F.path[0] ? name : " ");
        idunn_label_set_text(F.status_size, " ");
        return;
    }
    snprintf(text, sizeof(text), "%.0f %%", view_zoom() * 100);
    idunn_label_set_text(F.zoom_label, text);
    if (F.folder.count > 1)
        snprintf(text, sizeof(text), "%s   (%zu of %zu)", name, F.folder.current + 1,
                 F.folder.count);
    else
        snprintf(text, sizeof(text), "%s", name);
    idunn_label_set_text(F.status_name, text);
    snprintf(text, sizeof(text), "%d \xc3\x97 %d", picture_width(&F.picture),
             picture_height(&F.picture));
    idunn_label_set_text(F.status_size, text);
}

/** show a picture; on an error the empty state says why */
static void load(const char *path, bool read_folder)
{
    snprintf(F.path, sizeof(F.path), "%s", path);
    const char *why = NULL;
    gfx_buffer_t *image = image_load(path, &why);
    if (read_folder)
        folder_read(&F.folder, path);
    if (!image || !picture_set(&F.picture, image))
    {
        picture_free(&F.picture);
        snprintf(F.error, sizeof(F.error), "%s: %s.", strrchr(path, '/') + 1,
                 why ? why : "there is not enough memory");
        show_empty();
        freyja_changed();
        return;
    }
    F.error[0] = '\0';
    idunn_widget_free(empty);
    empty = NULL;
    idunn_set_hidden(F.canvas, false);
    idunn_set_focus(F.canvas);
    view_reset();
}

void freyja_step(int step)
{
    char *path = folder_step(&F.folder, step);
    if (path)
        load(path, false);
    free(path);
}

static void path_typed(const char *typed)
{
    char path[PATH_LEN];
    const char *home = getenv("HOME");
    if (typed[0] == '~')
        snprintf(path, sizeof(path), "%s%s", home ? home : "", typed + 1);
    else
        snprintf(path, sizeof(path), "%s", typed);
    if (path[0] == '/')
        load(path, true);
}

static void path_ok(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    path_dialog = NULL;
    path_typed(idunn_text_field_text(path_field));
}

static void path_submit(idunn_widget_t *field, void *data)
{
    (void)data;
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s", idunn_text_field_text(field));
    if (path_dialog)
        idunn_dialog_close(path_dialog);
    path_dialog = NULL;
    path_typed(path);
}

/** ask for a path (a file chooser is in the polish plan) */
static void open_dialog(void)
{
    char path[PATH_LEN];
    const char *home = getenv("HOME");
    if (F.path[0])
    {
        snprintf(path, sizeof(path), "%s", F.path);
        *(strrchr(path, '/') + 1) = '\0';
    }
    else
        snprintf(path, sizeof(path), "%s/", home && strcmp(home, "/") ? home : "");
    path_dialog = idunn_dialog("Open a picture", NULL);
    path_field = idunn_text_field("/path/to/picture.png");
    idunn_set_size(path_field, 420, 0);
    idunn_text_field_set_text(path_field, path);
    idunn_text_field_on_submit(path_field, path_submit, NULL);
    idunn_add(idunn_dialog_content(path_dialog), path_field);
    idunn_dialog_add_button(path_dialog, "Cancel", IDUNN_OUTLINE, NULL, NULL);
    idunn_dialog_add_button(path_dialog, "Open", IDUNN_PRIMARY, path_ok, NULL);
    idunn_dialog_open(holder, path_dialog);
    idunn_set_focus(path_field);
    idunn_text_field_select(path_field, strlen(path), (size_t)-1);
}

static void rotate(bool left)
{
    if (picture_rotate(&F.picture, left))
        view_reset();
}

static void clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    switch ((intptr_t)data)
    {
    case 'p':
        freyja_step(-1);
        break;
    case 'n':
        freyja_step(1);
        break;
    case '-':
        view_zoom_by(1 / ZOOM_STEP);
        break;
    case '+':
        view_zoom_by(ZOOM_STEP);
        break;
    case 'f':
        view_fit();
        break;
    case 'l':
        rotate(true);
        break;
    case 'r':
        rotate(false);
        break;
    default:
        break;
    }
}

static idunn_widget_t *tool(idunn_widget_t *row, const char *icon, const char *tip, char what)
{
    idunn_widget_t *b = idunn_add(row, idunn_icon_button(icon, IDUNN_GHOST, clicked,
                                                         (void *)(intptr_t)what));
    idunn_set_tooltip(b, tip);
    return b;
}

static void separator(idunn_widget_t *row)
{
    idunn_widget_t *sep = idunn_add(row, idunn_separator());
    idunn_set_size(sep, 0, 20);
}

static idunn_widget_t *toolbar(void)
{
    idunn_widget_t *row = idunn_row(1, 0);
    idunn_box_set_padding(row, 2, 3);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
    idunn_widget_t *open = idunn_add(row, idunn_icon_button("folder-open", IDUNN_GHOST,
                                                            clicked_open, NULL));
    idunn_set_tooltip(open, "Open (Ctrl+O)");
    separator(row);
    tool(row, "chevron-left", "The picture before (Left)", 'p');
    tool(row, "chevron-right", "The next picture (Right)", 'n');
    separator(row);
    tool(row, "zoom-out", "Zoom out (minus)", '-');
    F.zoom_label = idunn_add(row, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_set_size(F.zoom_label, 48, 0);
    tool(row, "zoom-in", "Zoom in (plus)", '+');
    tool(row, "expand", "Fit in the window (0)", 'f');
    separator(row);
    tool(row, "rotate-ccw", "Turn left (Shift+R)", 'l');
    tool(row, "rotate-cw", "Turn right (R)", 'r');
    return row;
}

static bool window_key(idunn_window_t *win, uint16_t code, uint32_t mods, const char *text,
                       void *data)
{
    (void)win;
    (void)text;
    (void)data;
    bool ctrl = mods & IDUNN_MOD_CTRL;
    bool shift = mods & IDUNN_MOD_SHIFT;
    if (ctrl && code == KEY_O)
        open_dialog();
    else if (ctrl && code == KEY_W)
        idunn_window_close(F.win);
    else if (ctrl)
        return false;
    else if (code == KEY_LEFT || code == KEY_PAGEUP || code == KEY_BACKSPACE)
        freyja_step(-1);
    else if (code == KEY_RIGHT || code == KEY_PAGEDOWN || code == KEY_SPACE)
        freyja_step(1);
    else if (code == KEY_EQUAL || code == KEY_KPPLUS)
        view_zoom_by(ZOOM_STEP);
    else if (code == KEY_MINUS || code == KEY_KPMINUS)
        view_zoom_by(1 / ZOOM_STEP);
    else if (code == KEY_0 || code == KEY_F)
        view_fit();
    else if (code == KEY_1)
        view_zoom_to(1.0);
    else if (code == KEY_R)
        rotate(shift);
    else
        return false;
    return true;
}

static void on_event(idunn_app_t *app, const heim_msg_t *msg, void *data)
{
    (void)app;
    (void)data;
    if (msg->type == HEIM_EV_SETTINGS && strcmp(msg->settings.name, "theme") == 0)
        view_redraw();
}

int main(int argc, char **argv)
{
    F.app = idunn_app_new("freyja");
    if (!F.app)
    {
        printf("freyja: no window server (start the desktop with startde)\n");
        return 1;
    }
    F.win = idunn_window_new(F.app, "Freyja", 900, 640);
    if (!F.win)
        return 1;

    idunn_widget_t *root = idunn_column(0, 0);
    idunn_add(root, toolbar());
    idunn_add(root, idunn_separator());
    holder = idunn_grow(idunn_add(root, idunn_column(0, 0)), 1);
    idunn_grow(idunn_add(holder, view_new()), 1);
    idunn_add(root, idunn_separator());
    idunn_widget_t *status = idunn_add(root, idunn_row(0, 0));
    idunn_box_set_padding(status, 1, 4);
    idunn_box_set_align(status, IDUNN_CENTER, IDUNN_SPACE_BETWEEN);
    F.status_name = idunn_add(status, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_label_set_color(F.status_name, IDUNN_COLOR_MUTED_FOREGROUND);
    F.status_size = idunn_add(status, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_label_set_color(F.status_size, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_window_set_root(F.win, root);
    idunn_window_on_key(F.win, window_key, NULL);
    idunn_app_on_event(F.app, on_event, NULL);

    if (argc > 1)
    {
        char path[PATH_LEN];
        char cwd[PATH_LEN];
        if (argv[1][0] != '/' && getcwd(cwd, sizeof(cwd)))
            snprintf(path, sizeof(path), "%s/%s", strcmp(cwd, "/") ? cwd : "", argv[1]);
        else
            snprintf(path, sizeof(path), "%s", argv[1]);
        load(path, true);
    }
    else
        show_empty();
    freyja_changed();
    int code = idunn_app_run(F.app);
    picture_free(&F.picture);
    folder_free(&F.folder);
    return code;
}

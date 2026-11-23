/**
 * @file main.c
 * @brief Saga, the text editor: the window, the toolbar, the status line
 *        and the window's keys.
 *
 * usage: saga [file]   (a file that is not there is made by the first save)
 *
 *   Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S   new, open, save, save as
 *   Ctrl+F, Ctrl+H, F3, Shift+F3           find, replace, the next, the one before
 *   Ctrl+plus, Ctrl+minus, Ctrl+0          a larger, a smaller, the normal text
 *   Ctrl+W                                 close
 * the editing keys are in editor.c.
 */

#include "saga.h"

#include <heimdall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>
#include <unistd.h>

saga_t S;

void show_message(const char *title, const char *text)
{
    idunn_widget_t *dialog = idunn_dialog(title, text);
    idunn_dialog_add_button(dialog, "OK", IDUNN_PRIMARY, NULL, NULL);
    idunn_dialog_open(S.canvas, dialog);
}

void saga_changed(void)
{
    const char *name = S.path[0] ? strrchr(S.path, '/') + 1 : "Untitled";
    char title[300];
    snprintf(title, sizeof(title), "%s%s", buffer_modified(&S.buffer) ? "\xe2\x80\xa2 " : "",
             name);
    idunn_window_set_title(S.win, title);

    pos_t caret = editor_caret();
    pos_t start, end;
    char place[96];
    int n = snprintf(place, sizeof(place), "Line %zu, column %zu", caret.line + 1,
                     caret.col + 1);
    if (editor_selection(&start, &end))
    {
        size_t length;
        char *text = buffer_range(&S.buffer, start, end, &length);
        size_t chars = 0;
        for (size_t i = 0; text && i < length; i++)
            chars += ((unsigned char)text[i] & 0xC0) != 0x80;
        free(text);
        snprintf(place + n, sizeof(place) - (size_t)n, " (%zu selected)", chars);
    }
    idunn_label_set_text(S.status_place, place);
    idunn_label_set_text(S.status_kind, S.buffer.crlf ? "UTF-8, CR LF" : "UTF-8, LF");
}

static void clicked_new(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    file_new();
}

static void clicked_open(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    file_open_dialog();
}

static void clicked_save(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    file_save();
}

static void clicked_undo(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    editor_undo();
    editor_focus();
}

static void clicked_redo(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    editor_redo();
    editor_focus();
}

static void clicked_find(idunn_widget_t *b, void *data)
{
    (void)b;
    find_show(data != NULL);
}

static void tool(idunn_widget_t *row, const char *icon, const char *tip, idunn_callback_t on_click,
                 void *data)
{
    idunn_widget_t *b = idunn_add(row, idunn_icon_button(icon, IDUNN_GHOST, on_click, data));
    idunn_set_tooltip(b, tip);
}

static idunn_widget_t *toolbar(void)
{
    idunn_widget_t *row = idunn_row(1, 0);
    idunn_box_set_padding(row, 2, 3);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
    tool(row, "file-plus", "New (Ctrl+N)", clicked_new, NULL);
    tool(row, "folder-open", "Open (Ctrl+O)", clicked_open, NULL);
    tool(row, "save", "Save (Ctrl+S)", clicked_save, NULL);
    idunn_widget_t *sep = idunn_add(row, idunn_separator());
    idunn_set_size(sep, 0, 20);
    tool(row, "undo-2", "Undo (Ctrl+Z)", clicked_undo, NULL);
    tool(row, "redo-2", "Redo (Ctrl+Y)", clicked_redo, NULL);
    sep = idunn_add(row, idunn_separator());
    idunn_set_size(sep, 0, 20);
    tool(row, "search", "Find (Ctrl+F)", clicked_find, NULL);
    tool(row, "replace", "Replace (Ctrl+H)", clicked_find, (void *)1);
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
    if (ctrl && code == KEY_N)
        file_new();
    else if (ctrl && code == KEY_O)
        file_open_dialog();
    else if (ctrl && shift && code == KEY_S)
        file_save_as();
    else if (ctrl && code == KEY_S)
        file_save();
    else if (ctrl && code == KEY_F)
        find_show(false);
    else if (ctrl && code == KEY_H)
        find_show(true);
    else if (code == KEY_F3)
        find_next(shift);
    else if (ctrl && (code == KEY_EQUAL || code == KEY_KPPLUS))
        editor_zoom(1);
    else if (ctrl && (code == KEY_MINUS || code == KEY_KPMINUS))
        editor_zoom(-1);
    else if (ctrl && code == KEY_0)
        editor_zoom(0);
    else if (ctrl && code == KEY_W)
    {
        if (file_close_window(S.win, NULL))
            idunn_window_close(S.win);
    }
    else if (code == KEY_ESC)
        find_hide();
    else
        return false;
    return true;
}

static void on_event(idunn_app_t *app, const heim_msg_t *msg, void *data)
{
    (void)app;
    (void)data;
    if (msg->type != HEIM_EV_SETTINGS)
        return;
    if (strcmp(msg->settings.name, "theme") == 0)
        editor_theme_changed();
    else if (strcmp(msg->settings.name, "saga") == 0)
        editor_zoom(0);
}

int main(int argc, char **argv)
{
    S.app = idunn_app_new("saga");
    if (!S.app)
    {
        printf("saga: no window server (start the desktop with startde)\n");
        return 1;
    }
    if (!buffer_init(&S.buffer))
        return 1;
    S.win = idunn_window_new(S.app, "Saga", 800, 560);
    if (!S.win)
        return 1;

    idunn_widget_t *root = idunn_column(0, 0);
    idunn_add(root, toolbar());
    idunn_add(root, idunn_separator());
    idunn_add(root, find_bar_new());
    idunn_grow(idunn_add(root, editor_new()), 1);
    idunn_add(root, idunn_separator());
    idunn_widget_t *status = idunn_add(root, idunn_row(0, 0));
    idunn_box_set_padding(status, 1, 4);
    idunn_box_set_align(status, IDUNN_CENTER, IDUNN_SPACE_BETWEEN);
    S.status_place = idunn_add(status, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_label_set_color(S.status_place, IDUNN_COLOR_MUTED_FOREGROUND);
    S.status_kind = idunn_add(status, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_label_set_color(S.status_kind, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_window_set_root(S.win, root);
    idunn_window_on_key(S.win, window_key, NULL);
    idunn_window_on_close(S.win, file_close_window, NULL);
    idunn_app_on_event(S.app, on_event, NULL);

    if (argc > 1)
    {
        char path[PATH_LEN];
        char cwd[PATH_LEN];
        if (argv[1][0] != '/' && getcwd(cwd, sizeof(cwd)))
            snprintf(path, sizeof(path), "%s/%s", strcmp(cwd, "/") ? cwd : "", argv[1]);
        else
            snprintf(path, sizeof(path), "%s", argv[1]);
        file_open(path);
    }
    saga_changed();
    editor_focus();
    int status_code = idunn_app_run(S.app);
    buffer_free(&S.buffer);
    return status_code;
}

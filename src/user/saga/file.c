/**
 * @file file.c
 * @brief Saga: open, save, save as, and "save the changes?".
 *
 * a save writes a new file next to the old one and renames it over it, so
 * a crash never leaves half a file. the file keeps its permissions. New,
 * Open and the close button ask first when there are unsaved changes.
 */

#include "saga.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FILE_MAX (32 * 1024 * 1024) // a larger file is not opened

// what comes after "save the changes?"
typedef enum
{
    THEN_NOTHING,
    THEN_NEW,
    THEN_OPEN,
    THEN_CLOSE,
} then_t;

static then_t then;
static idunn_widget_t *path_dialog;
static idunn_widget_t *path_field;
static void (*path_done)(const char *path);

static void error_message(const char *what, const char *path, int error)
{
    char text[PATH_LEN + 96];
    snprintf(text, sizeof(text), "%s: %s.", path, strerror(error));
    show_message(what, text);
}

bool file_open(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        if (errno != ENOENT)
        {
            error_message("Cannot open the file", path, errno);
            return false;
        }
        // a new file: it is made by the first save
        buffer_set_text(&S.buffer, "", 0);
        snprintf(S.path, sizeof(S.path), "%s", path);
        editor_reset();
        return true;
    }
    if (S_ISDIR(st.st_mode))
    {
        show_message("Cannot open a folder", path);
        return false;
    }
    if (st.st_size > FILE_MAX)
    {
        show_message("The file is too large", "Saga opens files of up to 32 MiB.");
        return false;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        error_message("Cannot open the file", path, errno);
        return false;
    }
    size_t size = (size_t)st.st_size;
    char *text = malloc(size + 1);
    size_t got = 0;
    while (text && got < size)
    {
        ssize_t n = read(fd, text + got, size - got);
        if (n <= 0)
            break;
        got += (size_t)n;
    }
    close(fd);
    if (!text)
    {
        show_message("Cannot open the file", "There is not enough memory.");
        return false;
    }
    // a zero byte: not text (a program, a picture)
    if (memchr(text, '\0', got))
    {
        free(text);
        show_message("This is not a text file", path);
        return false;
    }
    buffer_set_text(&S.buffer, text, got);
    free(text);
    snprintf(S.path, sizeof(S.path), "%s", path);
    editor_reset();
    return true;
}

/** write the buffer to path; true when it is saved */
static bool write_file(const char *path)
{
    size_t length;
    char *text = buffer_text(&S.buffer, &length);
    if (!text)
        return false;
    struct stat st;
    mode_t mode = stat(path, &st) == 0 ? st.st_mode & 07777 : 0644;
    char temp[PATH_LEN + 16];
    snprintf(temp, sizeof(temp), "%s.saving", path);
    int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC, mode);
    int error = fd < 0 ? errno : 0;
    for (size_t done = 0; !error && done < length;)
    {
        ssize_t n = write(fd, text + done, length - done);
        if (n <= 0)
            error = n < 0 ? errno : ENOSPC;
        else
            done += (size_t)n;
    }
    if (!error && fsync(fd) != 0)
        error = errno;
    if (fd >= 0 && close(fd) != 0 && !error)
        error = errno;
    if (!error && rename(temp, path) != 0)
        error = errno;
    free(text);
    if (error)
    {
        unlink(temp);
        error_message("Cannot save the file", path, error);
        return false;
    }
    chmod(path, mode);
    snprintf(S.path, sizeof(S.path), "%s", path);
    buffer_mark_saved(&S.buffer);
    saga_changed();
    return true;
}

static void do_then(void)
{
    then_t what = then;
    then = THEN_NOTHING;
    switch (what)
    {
    case THEN_NEW:
        buffer_set_text(&S.buffer, "", 0);
        S.path[0] = '\0';
        editor_reset();
        break;
    case THEN_OPEN:
        file_open_dialog();
        break;
    case THEN_CLOSE:
        idunn_window_close(S.win);
        break;
    default:
        break;
    }
}

/** the typed path, after its dialog closed */
static void path_typed(const char *typed)
{
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s", typed);
    if (path[0] == '~')
    {
        const char *home = getenv("HOME");
        char rest[PATH_LEN];
        snprintf(rest, sizeof(rest), "%s", path + 1);
        snprintf(path, sizeof(path), "%s%s", home ? home : "", rest);
    }
    if (path[0] != '/')
    {
        then = THEN_NOTHING;
        show_message("Not a path", "Type a whole path, from / or ~.");
        return;
    }
    path_done(path);
}

static void path_ok(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    // the button closed the dialog already
    path_dialog = NULL;
    path_typed(idunn_text_field_text(path_field));
}

static void path_cancelled(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    then = THEN_NOTHING;
}

/** Enter: close the dialog first, so a message can open after it */
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

/** a dialog that asks for a path, then calls done with it */
static void ask_path(const char *title, const char *button, const char *path,
                     void (*done)(const char *path))
{
    path_done = done;
    path_dialog = idunn_dialog(title, NULL);
    path_field = idunn_text_field("/path/to/file.txt");
    idunn_set_size(path_field, 420, 0);
    idunn_text_field_set_text(path_field, path);
    idunn_text_field_on_submit(path_field, path_submit, NULL);
    idunn_add(idunn_dialog_content(path_dialog), path_field);
    idunn_dialog_add_button(path_dialog, "Cancel", IDUNN_OUTLINE, path_cancelled, NULL);
    idunn_dialog_add_button(path_dialog, button, IDUNN_PRIMARY, path_ok, NULL);
    idunn_dialog_open(S.canvas, path_dialog);
    idunn_set_focus(path_field);
    // the name (after the last /) is selected, so typing replaces it
    const char *slash = strrchr(path, '/');
    idunn_text_field_select(path_field, slash ? (size_t)(slash + 1 - path) : 0, (size_t)-1);
}

/** the folder of the open file (or the home folder), with a / at its end */
static void start_folder(char *out, size_t length)
{
    if (S.path[0])
    {
        snprintf(out, length, "%s", S.path);
        char *slash = strrchr(out, '/');
        if (slash)
            slash[1] = '\0';
        return;
    }
    const char *home = getenv("HOME");
    snprintf(out, length, "%s%s", home ? home : "/",
             home && home[strlen(home) - 1] != '/' ? "/" : "");
}

static void open_chosen(const char *path)
{
    file_open(path);
}

static void save_as_chosen(const char *path)
{
    if (write_file(path))
        do_then();
    else
        then = THEN_NOTHING;
}

void file_save_as(void)
{
    char path[PATH_LEN];
    if (S.path[0])
        snprintf(path, sizeof(path), "%s", S.path);
    else
    {
        start_folder(path, sizeof(path));
        strncat(path, "untitled.txt", sizeof(path) - strlen(path) - 1);
    }
    ask_path("Save as", "Save", path, save_as_chosen);
}

void file_save(void)
{
    if (!S.path[0])
    {
        file_save_as();
        return;
    }
    if (write_file(S.path))
        do_then();
    else
        then = THEN_NOTHING;
}

static void dont_save(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    buffer_mark_saved(&S.buffer); // the changes are given up
    do_then();
}

static void save_first(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    file_save(); // then do_then, or save as and then do_then
}

/** go on with what, after asking about unsaved changes when there are any */
static void unless_unsaved(then_t what)
{
    then = what;
    if (!buffer_modified(&S.buffer))
    {
        do_then();
        return;
    }
    char title[300];
    const char *name = S.path[0] ? strrchr(S.path, '/') + 1 : "the new text";
    snprintf(title, sizeof(title), "Save the changes to %s?", name);
    idunn_widget_t *dialog = idunn_dialog(title, "If you do not save them, they are lost.");
    idunn_dialog_add_button(dialog, "Do not save", IDUNN_DESTRUCTIVE, dont_save, NULL);
    idunn_dialog_add_button(dialog, "Cancel", IDUNN_OUTLINE, path_cancelled, NULL);
    idunn_dialog_add_button(dialog, "Save", IDUNN_PRIMARY, save_first, NULL);
    idunn_dialog_open(S.canvas, dialog);
}

void file_new(void)
{
    unless_unsaved(THEN_NEW);
}

void file_open_dialog(void)
{
    if (then == THEN_NOTHING && buffer_modified(&S.buffer))
    {
        unless_unsaved(THEN_OPEN);
        return;
    }
    then = THEN_NOTHING;
    char path[PATH_LEN];
    start_folder(path, sizeof(path));
    ask_path("Open", "Open", path, open_chosen);
}

bool file_close_window(idunn_window_t *win, void *data)
{
    (void)win;
    (void)data;
    if (!buffer_modified(&S.buffer))
        return true;
    unless_unsaved(THEN_CLOSE);
    return false;
}

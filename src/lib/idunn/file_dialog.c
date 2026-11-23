/**
 * @file file_dialog.c
 * @brief idunn: a dialog to choose a file to open, or a name to save as.
 *
 * the folder's items are a list: folders first, then the files, by name
 * (letter case ignored); names that start with a dot are left out. a
 * double click or Enter opens a folder or chooses a file; the arrow
 * button goes to the folder above. to save, the name field holds the
 * name (a click on a file puts its name there), and a file that is
 * there already is replaced only after a question. a name with a / in
 * it is a path: a folder there is opened, a file chosen.
 */

#include "idunn_internal.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define PATH_MAX_LEN 1024
#define LIST_HEIGHT  300 // design pixels
#define DIALOG_WIDTH 520

typedef struct
{
    char *name;
    bool folder;
} file_entry_t;

static struct
{
    idunn_file_mode_t mode;
    idunn_file_done_t done;
    void *data;
    idunn_widget_t *owner;
    idunn_widget_t *dialog;
    idunn_widget_t *where; // the folder's path
    idunn_widget_t *list;
    idunn_widget_t *name;  // saving: the name field
    idunn_widget_t *empty; // "Nothing here"
    char folder[PATH_MAX_LEN];
    char replace[PATH_MAX_LEN]; // the path a "Replace?" question is about
    file_entry_t *entries;
    int count;
} F;

static void free_entries(void)
{
    for (int i = 0; i < F.count; i++)
        free(F.entries[i].name);
    free(F.entries);
    F.entries = NULL;
    F.count = 0;
}

static int by_kind_and_name(const void *pa, const void *pb)
{
    const file_entry_t *a = pa;
    const file_entry_t *b = pb;
    if (a->folder != b->folder)
        return a->folder ? -1 : 1;
    return strcasecmp(a->name, b->name);
}

/** a Tela picture for a name, from its extension */
static const char *picture_of(const file_entry_t *e)
{
    if (e->folder)
        return "folder";
    const char *dot = strrchr(e->name, '.');
    if (!dot)
        return "text-x-generic";
    static const char *const pictures[] = {".png", ".jpg", ".jpeg", ".bmp", ".ppm", ".pgm"};
    for (size_t i = 0; i < sizeof(pictures) / sizeof(pictures[0]); i++)
        if (strcasecmp(dot, pictures[i]) == 0)
            return "image-x-generic";
    if (strcasecmp(dot, ".wav") == 0)
        return "audio-x-generic";
    return "text-x-generic";
}

static void join(const char *folder, const char *name, char *out, size_t length)
{
    snprintf(out, length, "%s%s%s", folder, strcmp(folder, "/") == 0 ? "" : "/", name);
}

/** read a folder into the list; false when it cannot be read */
static bool fill(const char *folder)
{
    DIR *dir = opendir(folder);
    if (!dir)
        return false;
    free_entries();
    snprintf(F.folder, sizeof(F.folder), "%s", folder);
    int capacity = 0;
    struct dirent *d;
    while ((d = readdir(dir)))
    {
        if (d->d_name[0] == '.')
            continue;
        if (F.count == capacity)
        {
            capacity = capacity ? capacity * 2 : 64;
            file_entry_t *more = realloc(F.entries, (size_t)capacity * sizeof(*more));
            if (!more)
                break;
            F.entries = more;
        }
        char path[PATH_MAX_LEN];
        join(folder, d->d_name, path, sizeof(path));
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        F.entries[F.count].name = strdup(d->d_name);
        F.entries[F.count].folder = S_ISDIR(st.st_mode);
        if (F.entries[F.count].name)
            F.count++;
    }
    closedir(dir);
    qsort(F.entries, (size_t)F.count, sizeof(*F.entries), by_kind_and_name);

    idunn_list_view_clear(F.list);
    for (int i = 0; i < F.count; i++)
        idunn_list_view_add(F.list, picture_of(&F.entries[i]), F.entries[i].name, NULL);
    idunn_label_set_text(F.where, F.folder);
    idunn_set_hidden(F.empty, F.count > 0);
    return true;
}

/** the dialog is done: close it, then hand on the path */
static void finish(const char *path)
{
    char chosen[PATH_MAX_LEN];
    snprintf(chosen, sizeof(chosen), "%s", path);
    idunn_file_done_t done = F.done;
    void *data = F.data;
    idunn_dialog_close(F.dialog);
    F.dialog = NULL;
    free_entries();
    if (done)
        done(chosen, data);
}

static void replace_confirmed(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    // the question is closed: hand on the path
    if (F.done)
        F.done(F.replace, F.data);
}

/** a path to save to: a question first when there is a file there */
static void save_to(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        finish(path);
        return;
    }
    snprintf(F.replace, sizeof(F.replace), "%s", path);
    idunn_widget_t *owner = F.owner;
    idunn_dialog_close(F.dialog);
    F.dialog = NULL;
    free_entries();
    char title[300];
    const char *slash = strrchr(path, '/');
    snprintf(title, sizeof(title), "Replace %s?", slash ? slash + 1 : path);
    idunn_widget_t *ask = idunn_dialog(title, "A file with this name already exists. "
                                              "Replacing it overwrites its contents.");
    idunn_dialog_add_button(ask, "Cancel", IDUNN_OUTLINE, NULL, NULL);
    idunn_dialog_add_button(ask, "Replace", IDUNN_DESTRUCTIVE, replace_confirmed, NULL);
    idunn_dialog_open(owner, ask);
}

/** a typed name or path: open a folder, or choose the file */
static void typed_path(const char *typed)
{
    char path[PATH_MAX_LEN];
    if (typed[0] == '/')
        snprintf(path, sizeof(path), "%s", typed);
    else if (typed[0] == '~')
    {
        const char *home = getenv("HOME");
        snprintf(path, sizeof(path), "%s%s", home ? home : "", typed + 1);
    }
    else
        join(F.folder, typed, path, sizeof(path));
    struct stat st;
    bool there = stat(path, &st) == 0;
    if (there && S_ISDIR(st.st_mode))
    {
        fill(path);
        if (F.name)
            idunn_text_field_set_text(F.name, "");
        return;
    }
    if (F.mode == IDUNN_FILE_SAVE)
        save_to(path);
    else if (there)
        finish(path);
}

/** the main button, Enter in the name field, or a double click on a file */
static void choose(void)
{
    if (F.mode == IDUNN_FILE_SAVE)
    {
        const char *name = idunn_text_field_text(F.name);
        if (name[0])
            typed_path(name);
        return;
    }
    int item = idunn_list_view_current(F.list);
    if (item >= 0 && item < F.count && idunn_list_view_selected(F.list, item))
        typed_path(F.entries[item].name);
}

static void activated(idunn_widget_t *list, void *data)
{
    (void)data;
    int item = idunn_list_view_current(list);
    if (item < 0 || item >= F.count)
        return;
    if (F.entries[item].folder)
    {
        char path[PATH_MAX_LEN];
        join(F.folder, F.entries[item].name, path, sizeof(path));
        fill(path);
        return;
    }
    if (F.mode == IDUNN_FILE_SAVE)
        idunn_text_field_set_text(F.name, F.entries[item].name);
    choose();
}

static void selected(idunn_widget_t *list, void *data)
{
    (void)data;
    int item = idunn_list_view_current(list);
    // saving: a click on a file takes its name
    if (F.name && item >= 0 && item < F.count && !F.entries[item].folder)
        idunn_text_field_set_text(F.name, F.entries[item].name);
}

static void up_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    char above[PATH_MAX_LEN];
    snprintf(above, sizeof(above), "%s", F.folder);
    char *slash = strrchr(above, '/');
    if (!slash)
        return;
    if (slash == above)
        slash[1] = '\0';
    else
        *slash = '\0';
    fill(above);
}

static void main_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    choose();
}

static void name_submitted(idunn_widget_t *field, void *data)
{
    (void)field;
    (void)data;
    choose();
}

static void cancel_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    idunn_dialog_close(F.dialog);
    F.dialog = NULL;
    free_entries();
}

void idunn_file_dialog(idunn_widget_t *owner, idunn_file_mode_t mode, const char *title,
                       const char *path, idunn_file_done_t done, void *data)
{
    if (!owner || !owner->window || owner->window->modal)
        return;
    // the folder to start in, and for saving the name in path
    char folder[PATH_MAX_LEN];
    snprintf(folder, sizeof(folder), "%s", path && path[0] ? path : "/");
    const char *name = "";
    struct stat st;
    if (stat(folder, &st) != 0 || !S_ISDIR(st.st_mode))
    {
        char *slash = strrchr(folder, '/');
        name = path + (slash ? slash - folder + 1 : 0);
        if (slash == folder)
            slash[1] = '\0';
        else if (slash)
            *slash = '\0';
        if (stat(folder, &st) != 0 || !S_ISDIR(st.st_mode))
            snprintf(folder, sizeof(folder), "/");
    }

    F.mode = mode;
    F.done = done;
    F.data = data;
    F.owner = owner;
    F.dialog = idunn_dialog(title, NULL);
    idunn_widget_t *content = idunn_dialog_content(F.dialog);
    idunn_set_size(content, DIALOG_WIDTH, 0);

    idunn_widget_t *head = idunn_add(content, idunn_row(2, 0));
    idunn_box_set_align(head, IDUNN_CENTER, IDUNN_START);
    idunn_widget_t *up = idunn_add(head, idunn_icon_button("arrow-up", IDUNN_OUTLINE, up_clicked,
                                                           NULL));
    idunn_button_set_size(up, IDUNN_SIZE_SMALL);
    idunn_set_tooltip(up, "Parent folder");
    F.where = idunn_grow(idunn_add(head, idunn_label(" ", IDUNN_TEXT_SMALL)), 1);
    idunn_label_set_color(F.where, IDUNN_COLOR_MUTED_FOREGROUND);

    F.list = idunn_list_view();
    idunn_list_view_set_pictures(F.list, true);
    idunn_list_view_set_framed(F.list, true);
    idunn_list_view_on_activate(F.list, activated, NULL);
    idunn_list_view_on_select(F.list, selected, NULL);
    idunn_widget_t *scroll = idunn_add(content, idunn_scroll_view(F.list));
    idunn_set_size(scroll, 0, LIST_HEIGHT);
    F.empty = idunn_add(content, idunn_label("This folder is empty.", IDUNN_TEXT_SMALL));
    idunn_label_set_color(F.empty, IDUNN_COLOR_MUTED_FOREGROUND);

    F.name = NULL;
    if (mode == IDUNN_FILE_SAVE)
    {
        F.name = idunn_add(content, idunn_text_field("Name"));
        idunn_text_field_set_text(F.name, name);
        idunn_text_field_on_submit(F.name, name_submitted, NULL);
    }

    idunn_widget_t *buttons = idunn_add(content, idunn_row(2, 0));
    idunn_box_set_align(buttons, IDUNN_CENTER, IDUNN_END);
    idunn_add(buttons, idunn_button("Cancel", IDUNN_OUTLINE, cancel_clicked, NULL));
    idunn_add(buttons, idunn_button(mode == IDUNN_FILE_SAVE ? "Save" : "Open", IDUNN_PRIMARY,
                                    main_clicked, NULL));

    fill(folder);
    idunn_dialog_open(owner, F.dialog);
    if (F.name)
    {
        idunn_set_focus(F.name);
        // the name without its extension is selected
        const char *dot = strrchr(name, '.');
        idunn_text_field_select(F.name, 0, dot && dot != name ? (size_t)(dot - name)
                                                              : (size_t)-1);
    }
    else
        idunn_set_focus(F.list);
}

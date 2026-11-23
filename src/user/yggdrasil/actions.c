/**
 * @file actions.c
 * @brief yggdrasil: what the user does to files: the context menus, new
 *        folders and files, rename, the trash, delete, properties, and
 *        copy, cut and paste through the clipboard.
 *
 * the clipboard holds the paths, one per line (pasted in mimir they are
 * the paths). a paste of what this window cut moves the files; any
 * other paste copies them.
 */

#include "fileops.h"
#include "yggdrasil.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <heimdall.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHOSEN_MAX 1024

static idunn_menu_t *menu;
static char *cut_text; // the clipboard text of the last cut, until a paste moves it

/** the path of entry i of the open folder */
static void entry_path(int i, char *out, size_t length)
{
    path_join(Y.folder.path, Y.folder.entries[i].name, out, length);
}

/** the selected paths (malloc'd strings in a malloc'd array) */
static char **chosen_paths(int *count)
{
    static int chosen[CHOSEN_MAX];
    *count = view_selection(chosen, CHOSEN_MAX);
    char **paths = *count ? calloc((size_t)*count, sizeof(char *)) : NULL;
    for (int i = 0; paths && i < *count; i++)
    {
        char path[PATH_LEN];
        entry_path(chosen[i], path, sizeof(path));
        paths[i] = strdup(path);
    }
    if (!paths)
        *count = 0;
    return paths;
}

static void free_paths(char **paths, int count)
{
    for (int i = 0; i < count; i++)
        free(paths[i]);
    free(paths);
}

/** the one selected entry, or NULL */
static const entry_t *one_chosen(void)
{
    int chosen[2];
    if (view_selection(chosen, 2) != 1)
        return NULL;
    return &Y.folder.entries[chosen[0]];
}

void actions_copy(bool cut)
{
    int count;
    char **paths = chosen_paths(&count);
    if (count == 0)
        return;
    size_t length = 0;
    for (int i = 0; i < count; i++)
        length += strlen(paths[i]) + 1;
    char *text = malloc(length + 1);
    if (text)
    {
        text[0] = '\0';
        for (int i = 0; i < count; i++)
        {
            strcat(text, paths[i]);
            if (i + 1 < count)
                strcat(text, "\n");
        }
        heim_clipboard_set(idunn_app_conn(Y.app), text, strlen(text));
        free(cut_text);
        cut_text = cut ? text : NULL;
        if (!cut)
            free(text);
    }
    free_paths(paths, count);
}

static void pasted(const char *text, size_t length, void *data)
{
    (void)data;
    (void)length;
    bool move = cut_text && strcmp(text, cut_text) == 0;
    // the lines that are paths of things that are there
    char *copy = strdup(text);
    char **paths = NULL;
    int count = 0;
    int capacity = 0;
    for (char *line = copy ? strtok(copy, "\n") : NULL; line; line = strtok(NULL, "\n"))
    {
        struct stat st;
        if (line[0] != '/' || lstat(line, &st) != 0)
            continue;
        if (count == capacity)
        {
            capacity = capacity ? capacity * 2 : 16;
            char **more = realloc(paths, (size_t)capacity * sizeof(char *));
            if (!more)
                break;
            paths = more;
        }
        paths[count++] = strdup(line);
    }
    free(copy);
    if (count == 0)
        show_message("Nothing to paste", "Copy or cut files first.");
    else
    {
        job_start(move ? JOB_MOVE : JOB_COPY, paths, count, Y.folder.path);
        if (move)
        {
            // a cut goes once: the clipboard keeps the paths, now as a copy
            free(cut_text);
            cut_text = NULL;
        }
    }
    free_paths(paths, count);
}

void actions_paste(void)
{
    if (view_in_trash())
    {
        show_message("Cannot paste here", "Move things to the trash with Delete.");
        return;
    }
    idunn_app_paste(Y.app, pasted, NULL);
}

/** false (with a message) when a typed name cannot be a file's name */
static bool good_name(const char *name)
{
    if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        strchr(name, '/'))
    {
        show_message("Not a name", "A name cannot be empty, . or .., or have a / in it.");
        return false;
    }
    return true;
}

/** the item of the view that shows a name, or -1 */
static int item_of(const char *name)
{
    for (int i = 0; i < Y.folder.count; i++)
        if (strcmp(Y.folder.entries[i].name, name) == 0)
            return i;
    return -1;
}

/** the bytes of a name to select for a new one: all but the extension */
static size_t name_part(const char *name, bool folder)
{
    const char *dot = strrchr(name, '.');
    return dot && dot != name && !folder ? (size_t)(dot - name) : strlen(name);
}

static void report_error(const char *what, const char *name, int error)
{
    char text[400];
    snprintf(text, sizeof(text), "%s: %s.", name, strerror(error));
    show_message(what, text);
}

static char renaming[256];

/** after a rename: the new name selected, and the list has the keyboard */
static void select_only_name(const char *name)
{
    for (int i = 0; Y.list && i < Y.folder.count; i++)
        idunn_list_view_select(Y.list, i, false);
    view_select_name(name);
}

/** the name field of the list is done: rename to the typed name */
static void rename_edited(idunn_widget_t *list, int item, const char *name, void *data)
{
    (void)list;
    (void)item;
    (void)data;
    if (!name || strcmp(name, renaming) == 0 || !good_name(name))
        return;
    char from[PATH_LEN];
    char to[PATH_LEN];
    path_join(Y.folder.path, renaming, from, sizeof(from));
    path_join(Y.folder.path, name, to, sizeof(to));
    struct stat st;
    if (lstat(to, &st) == 0)
    {
        report_error("Cannot rename", name, EEXIST);
        return;
    }
    if (rename(from, to) != 0)
    {
        report_error("Cannot rename", renaming, errno);
        return;
    }
    view_refresh();
    select_only_name(name);
}

/** the name of an item becomes a text field in the view (F2) */
static void edit_name(const char *name, bool folder)
{
    int item = item_of(name);
    if (item < 0 || !Y.list)
        return;
    snprintf(renaming, sizeof(renaming), "%s", name);
    select_only_name(name);
    // the name without its extension is selected, so a new name keeps it
    idunn_list_view_edit(Y.list, item, 0, name_part(name, folder), rename_edited, NULL);
}

void actions_rename(void)
{
    const entry_t *e = one_chosen();
    if (e)
        edit_name(e->name, S_ISDIR(e->mode));
}

/**
 * a new folder or file is made at once with a free name ("New folder 2"),
 * and its name is then a text field, as a rename.
 */
void actions_new(bool folder)
{
    if (view_in_trash())
        return;
    char path[PATH_LEN];
    fileops_unique(Y.folder.path, folder ? "New folder" : "New file", path, sizeof(path));
    int fd = -1;
    if (folder ? mkdir(path, 0755) != 0 : (fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0)
    {
        report_error(folder ? "Cannot make the folder" : "Cannot make the file", path_name(path),
                     errno);
        return;
    }
    if (fd >= 0)
        close(fd);
    view_refresh();
    edit_name(path_name(path), folder);
}

static char **delete_paths;
static int delete_count;

static void delete_confirmed(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    job_start(JOB_DELETE, delete_paths, delete_count, NULL);
    free_paths(delete_paths, delete_count);
    delete_paths = NULL;
    delete_count = 0;
}

static void delete_cancelled(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    free_paths(delete_paths, delete_count);
    delete_paths = NULL;
    delete_count = 0;
}

void actions_delete(void)
{
    free_paths(delete_paths, delete_count);
    delete_paths = chosen_paths(&delete_count);
    if (delete_count == 0)
        return;
    char title[300];
    if (delete_count == 1)
        snprintf(title, sizeof(title), "Delete \"%s\" for good?", path_name(delete_paths[0]));
    else
        snprintf(title, sizeof(title), "Delete %d things for good?", delete_count);
    idunn_widget_t *dialog = idunn_dialog(title, "This cannot be undone.");
    idunn_dialog_add_button(dialog, "Cancel", IDUNN_OUTLINE, delete_cancelled, NULL);
    idunn_dialog_add_button(dialog, "Delete", IDUNN_DESTRUCTIVE, delete_confirmed, NULL);
    idunn_dialog_open(view_widget(), dialog);
}

void actions_trash(void)
{
    if (view_in_trash())
    {
        actions_delete();
        return;
    }
    int count;
    char **paths = chosen_paths(&count);
    // things on another disk than the trash cannot go in: offer to delete them
    free_paths(delete_paths, delete_count);
    delete_paths = calloc((size_t)(count ? count : 1), sizeof(char *));
    delete_count = 0;
    int failed = 0;
    int first_error = 0;
    const char *first_name = NULL;
    for (int i = 0; i < count; i++)
    {
        int error = trash_put(Y.trash, paths[i], time(NULL));
        if (error == EXDEV && delete_paths)
            delete_paths[delete_count++] = strdup(paths[i]);
        else if (error && failed++ == 0)
        {
            first_error = error;
            first_name = path_name(paths[i]);
        }
    }
    view_refresh();
    if (failed)
    {
        char text[400];
        snprintf(text, sizeof(text), "%s: %s.%s", first_name, strerror(first_error),
                 failed > 1 ? " Other things could not go to the trash either." : "");
        show_message("Cannot move to the trash", text);
    }
    else if (delete_count > 0)
    {
        char text[400];
        if (delete_count == 1)
            snprintf(text, sizeof(text), "\"%s\" is on another disk than the trash. Delete it "
                     "for good?", path_name(delete_paths[0]));
        else
            snprintf(text, sizeof(text), "%d things are on another disk than the trash. Delete "
                     "them for good?", delete_count);
        idunn_widget_t *dialog = idunn_dialog("Cannot move to the trash", text);
        idunn_dialog_add_button(dialog, "Cancel", IDUNN_OUTLINE, delete_cancelled, NULL);
        idunn_dialog_add_button(dialog, "Delete", IDUNN_DESTRUCTIVE, delete_confirmed, NULL);
        idunn_dialog_open(view_widget(), dialog);
    }
    free_paths(paths, count);
}

void actions_restore(void)
{
    int chosen[CHOSEN_MAX];
    int count = view_selection(chosen, CHOSEN_MAX);
    for (int i = 0; i < count; i++)
    {
        char restored[PATH_LEN];
        const char *name = Y.folder.entries[chosen[i]].name;
        int error = trash_restore(Y.trash, name, restored, sizeof(restored));
        if (error)
        {
            report_error("Cannot restore", name, error);
            break;
        }
    }
    view_refresh();
}

static void empty_confirmed(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    int error = trash_empty(Y.trash);
    if (error)
        report_error("Cannot empty the trash", "Trash", error);
    view_refresh();
}

void actions_empty_trash(void)
{
    idunn_widget_t *dialog = idunn_dialog("Empty the trash?",
                                          "All in the trash is deleted for good.");
    idunn_dialog_add_button(dialog, "Cancel", IDUNN_OUTLINE, NULL, NULL);
    idunn_dialog_add_button(dialog, "Empty", IDUNN_DESTRUCTIVE, empty_confirmed, NULL);
    idunn_dialog_open(view_widget(), dialog);
}

// properties: the mode bits behind the nine check boxes
static const mode_t mode_bits[9] = {S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP,
                                    S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH};
static idunn_widget_t *mode_boxes[9];
static char properties_path[PATH_LEN];
static mode_t properties_mode;

static void properties_apply(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    mode_t mode = properties_mode & ~(mode_t)0777;
    for (int i = 0; i < 9; i++)
        if (idunn_check_box_state(mode_boxes[i]) == IDUNN_CHECKED)
            mode |= mode_bits[i];
    if ((mode & 0777) != (properties_mode & 0777) && chmod(properties_path, mode & 07777) != 0)
        report_error("Cannot change the permissions", path_name(properties_path), errno);
    view_refresh();
}

static void fact(idunn_widget_t *box, const char *name, const char *value)
{
    idunn_widget_t *row = idunn_add(box, idunn_row(4, 0));
    idunn_widget_t *label = idunn_add(row, idunn_label(name, IDUNN_TEXT_BODY));
    idunn_label_set_color(label, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_set_size(label, 96, 0);
    idunn_widget_t *text = idunn_grow(idunn_add(row, idunn_label(value, IDUNN_TEXT_BODY)), 1);
    idunn_label_set_wrap(text, true);
}

void actions_properties(void)
{
    const entry_t *e = one_chosen();
    if (!e)
        return;
    path_join(Y.folder.path, e->name, properties_path, sizeof(properties_path));
    properties_mode = e->mode;

    idunn_widget_t *dialog = idunn_dialog(e->name, NULL);
    idunn_widget_t *box = idunn_column(2, 0);
    idunn_add(idunn_dialog_content(dialog), box);
    char text[PATH_LEN];
    fact(box, "Type", e->type);
    fact(box, "In", Y.folder.path);
    if (S_ISDIR(e->mode))
    {
        folder_t inside = {0};
        folder_read(&inside, properties_path, true, NULL);
        snprintf(text, sizeof(text), "%d %s", inside.count, inside.count == 1 ? "item" : "items");
        folder_free(&inside);
    }
    else
    {
        format_size((uint64_t)e->size, text, sizeof(text));
        char bytes[48];
        snprintf(bytes, sizeof(bytes), " (%lu bytes)", (unsigned long)e->size);
        if (e->size >= 1024)
            strncat(text, bytes, sizeof(text) - strlen(text) - 1);
    }
    fact(box, "Size", text);
    format_time(e->mtime, text, sizeof(text));
    fact(box, "Changed", text);
    struct passwd *pw = getpwuid(e->uid);
    struct group *gr = getgrgid(e->gid);
    snprintf(text, sizeof(text), "%s, group %s", pw ? pw->pw_name : "?", gr ? gr->gr_name : "?");
    fact(box, "Owner", text);

    idunn_widget_t *grid = idunn_add(box, idunn_column(1, 0));
    static const char *const who[] = {"Owner", "Group", "Others"};
    static const char *const what[] = {"Read", "Write", "Run"};
    bool mine = getuid() == 0 || getuid() == e->uid;
    for (int r = 0; r < 3; r++)
    {
        idunn_widget_t *row = idunn_add(grid, idunn_row(4, 0));
        idunn_widget_t *label = idunn_add(row, idunn_label(who[r], IDUNN_TEXT_BODY));
        idunn_label_set_color(label, IDUNN_COLOR_MUTED_FOREGROUND);
        idunn_set_size(label, 96, 0);
        for (int c = 0; c < 3; c++)
        {
            int i = r * 3 + c;
            mode_boxes[i] = idunn_add(row, idunn_check_box(what[c], e->mode & mode_bits[i], NULL,
                                                           NULL));
            idunn_set_disabled(mode_boxes[i], !mine);
        }
    }
    idunn_dialog_add_button(dialog, "Cancel", IDUNN_OUTLINE, NULL, NULL);
    idunn_dialog_add_button(dialog, "Apply", IDUNN_PRIMARY, properties_apply, NULL);
    idunn_dialog_open(view_widget(), dialog);
}

static void do_open(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    const entry_t *e = one_chosen();
    if (e)
        open_entry(e);
}

static void do_mimir(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    const entry_t *e = one_chosen();
    char path[PATH_LEN];
    if (!e)
        open_in_mimir(Y.folder.path, NULL);
    else if (S_ISDIR(e->mode))
    {
        path_join(Y.folder.path, e->name, path, sizeof(path));
        open_in_mimir(path, NULL);
    }
    else
    {
        path_join(Y.folder.path, e->name, path, sizeof(path));
        open_in_mimir(Y.folder.path, path);
    }
}

static void do_cut(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    actions_copy(true);
}

static void do_copy(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    actions_copy(false);
}

static void do_paste(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    actions_paste();
}

static void do_rename(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    actions_rename();
}

static void do_trash(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    actions_trash();
}

static void do_delete(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    actions_delete();
}

static void do_restore(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    actions_restore();
}

static void do_empty(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    actions_empty_trash();
}

static void do_properties(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    actions_properties();
}

static void do_new(idunn_widget_t *w, void *data)
{
    (void)w;
    actions_new(data != NULL);
}

static void do_hidden(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    Y.show_hidden = !Y.show_hidden;
    view_refresh();
}

void actions_item_menu(void)
{
    if (menu)
        idunn_menu_free(menu);
    menu = idunn_menu_new();
    int chosen[2];
    int count = view_selection(chosen, 2);
    const entry_t *e = count == 1 ? &Y.folder.entries[chosen[0]] : NULL;
    bool trash = view_in_trash();

    if (trash && count > 0)
    {
        idunn_menu_add(menu, "rotate-ccw", "Restore", NULL, do_restore, NULL);
        idunn_menu_add(menu, "trash", "Delete for good...", "Delete", do_delete, NULL);
    }
    else if (trash)
    {
        int item = idunn_menu_add(menu, "trash", "Empty the trash...", NULL, do_empty, NULL);
        idunn_menu_set_disabled(menu, item, Y.folder.count == 0);
    }
    else if (count > 0)
    {
        if (e)
        {
            idunn_menu_add(menu, S_ISDIR(e->mode) ? "folder-open" : "external-link", "Open",
                           "Enter", do_open, NULL);
            bool runs = !S_ISDIR(e->mode) && (e->mode & S_IXUSR);
            if (S_ISDIR(e->mode) || runs)
                idunn_menu_add(menu, "square-terminal", runs ? "Run in Mimir" : "Open in Mimir",
                               NULL, do_mimir, NULL);
            idunn_menu_add_separator(menu);
        }
        idunn_menu_add(menu, "scissors", "Cut", "Ctrl+X", do_cut, NULL);
        idunn_menu_add(menu, "copy", "Copy", "Ctrl+C", do_copy, NULL);
        if (e)
            idunn_menu_add(menu, "pencil", "Rename", "F2", do_rename, NULL);
        idunn_menu_add_separator(menu);
        idunn_menu_add(menu, "trash", "Move to the trash", "Delete", do_trash, NULL);
        idunn_menu_add(menu, NULL, "Delete for good...", "Shift+Delete", do_delete, NULL);
        if (e)
        {
            idunn_menu_add_separator(menu);
            idunn_menu_add(menu, "info", "Properties", NULL, do_properties, NULL);
        }
    }
    else
    {
        idunn_menu_add(menu, "folder-plus", "New folder", "Ctrl+Shift+N", do_new, (void *)1);
        idunn_menu_add(menu, "file-plus", "New file", NULL, do_new, NULL);
        idunn_menu_add(menu, "clipboard-paste", "Paste", "Ctrl+V", do_paste, NULL);
        idunn_menu_add_separator(menu);
        idunn_menu_add(menu, "square-terminal", "Open in Mimir", NULL, do_mimir, NULL);
        idunn_menu_add_check(menu, "Hidden files", Y.show_hidden, do_hidden, NULL);
    }
    idunn_menu_popup_at_pointer(menu, view_widget());
}

bool actions_key(idunn_window_t *win, uint16_t code, uint32_t mods, const char *text,
                 void *data)
{
    (void)win;
    (void)text;
    (void)data;
    bool ctrl = mods & IDUNN_MOD_CTRL;
    bool shift = mods & IDUNN_MOD_SHIFT;
    bool alt = mods & IDUNN_MOD_ALT;
    if (code == KEY_BACKSPACE || (alt && code == KEY_UP))
        go_up();
    else if (alt && code == KEY_LEFT)
        go_back();
    else if (alt && code == KEY_RIGHT)
        go_forward();
    else if (ctrl && code == KEY_L)
        toolbar_edit_path();
    else if (ctrl && code == KEY_H)
    {
        Y.show_hidden = !Y.show_hidden;
        view_refresh();
    }
    else if (ctrl && code == KEY_C)
        actions_copy(false);
    else if (ctrl && code == KEY_X)
        actions_copy(true);
    else if (ctrl && code == KEY_V)
        actions_paste();
    else if (ctrl && shift && code == KEY_N)
        actions_new(true);
    else if (code == KEY_F2)
        actions_rename();
    else if (code == KEY_F5)
        view_refresh();
    else if (code == KEY_DELETE)
        shift ? actions_delete() : actions_trash();
    else if (code == KEY_ESC)
        toolbar_update(); // leaves the typed path
    else
        return false;
    return true;
}

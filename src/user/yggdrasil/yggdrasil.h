#pragma once

/**
 * @file yggdrasil.h
 * @brief yggdrasil, the file manager: the parts its files share.
 *
 * the files:
 *   main.c     the window, the keys, the refresh timer
 *   folder.c   reading a folder: names, sizes, times, file types
 *   fileops.c  copy, move, delete, the trash (plain POSIX, host tested)
 *   view.c     the folder as a list or a grid, the status line
 *   toolbar.c  back, forward, up, the path bar, the view buttons
 *   places.c   the sidebar: home, its folders, the computer, the trash
 *   open.c     opening a file: the program for its type (open-with.conf)
 *   actions.c  the context menus, new, rename, properties, the clipboard
 *   jobs.c     copy, move and delete in a child process, with progress
 */

#include <idunn.h>
#include <mime.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define PATH_LEN    1024
#define HISTORY_MAX 32

typedef struct
{
    char *name;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    off_t size;
    uint32_t mtime;
    const char *type; // "text/plain" (the mime table's string)
    const char *icon; // a Tela icon name
} entry_t;

typedef enum
{
    SORT_NAME, // A to Z
    SORT_SIZE, // the largest first
    SORT_TYPE, // by the type, then the name
    SORT_DATE, // the newest first
} sort_t;

typedef struct
{
    char path[PATH_LEN];
    entry_t *entries;
    int count;
    int error; // errno when the folder cannot be read, else 0
} folder_t;

typedef struct
{
    idunn_app_t *app;
    idunn_window_t *win;
    mime_table_t *mime;
    char home[PATH_LEN];
    char trash[PATH_LEN]; // ~/.local/share/Trash

    folder_t folder;
    char filter[128]; // only the names with this in them ("" for all)
    sort_t sort;
    bool sort_reverse;
    bool show_hidden;
    bool grid;

    char *back[HISTORY_MAX];
    int back_count;
    char *forward[HISTORY_MAX];
    int forward_count;

    // the widgets
    idunn_widget_t *toolbar;
    idunn_widget_t *places;
    idunn_widget_t *view_holder; // a column the view is in
    idunn_widget_t *scroll;      // the scroll view around the list
    idunn_widget_t *list;        // the list or the grid (NULL: an empty state)
    idunn_widget_t *status;
} ygg_t;

extern ygg_t Y;

// folder.c

/**
 * read a folder, folders first, then by name (letter case ignored).
 * @param hidden also the names that start with a dot.
 * @return false when it cannot be read (f->error tells why; f is empty).
 */
bool folder_read(folder_t *f, const char *path, bool hidden, const mime_table_t *mime);
void folder_free(folder_t *f);
/** order the entries: folders first, then by the key (reversed: the other way) */
void folder_sort(folder_t *f, sort_t by, bool reverse);
/** keep only the entries whose names have text in them (letter case ignored) */
void folder_filter(folder_t *f, const char *text);
/** the same names, sizes and times (a refresh changes nothing) */
bool folder_same(const folder_t *a, const folder_t *b);
/** "12 bytes", "4.2 KiB", "650 MiB" */
void format_size(uint64_t size, char *out, size_t length);
/** "2026-11-22 14:03" in local time */
void format_time(uint32_t time, char *out, size_t length);
/** the Tela icon of a folder: user-home for the home folder, and so on */
const char *folder_icon(const char *path);
void path_join(const char *dir, const char *name, char *out, size_t length);
/** the folder above path ("/" stays "/") */
void path_parent(const char *path, char *out, size_t length);
/** the last part of a path ("/" for the root) */
const char *path_name(const char *path);

// view.c

void view_build(void);
/** read the folder again; the view changes only when the folder did */
void view_refresh(void);
/** the selected entries (indexes into Y.folder.entries); returns how many */
int view_selection(int *out, int max);
void view_select_name(const char *name);
void view_set_grid(bool grid);
void view_status(void);
/** the view widget for menus and dialogs (the list, or the holder) */
idunn_widget_t *view_widget(void);
/** true when the open folder is the trash's files folder */
bool view_in_trash(void);

// toolbar.c

idunn_widget_t *toolbar_build(void);
/** the path bar and the buttons follow Y.folder.path */
void toolbar_update(void);
/** a text field to type a path (Ctrl+L) */
void toolbar_edit_path(void);
/** the order of the last time (yggdrasil.conf), before the toolbar is built */
void toolbar_load_sort(void);
/** true while the filter field has the keyboard */
bool toolbar_filtering(void);
/** the filter field gets the keyboard (Ctrl+F) */
void toolbar_find(void);
/** empty the filter (another folder, or Escape); false when it was empty */
bool toolbar_clear_filter(void);

// places.c

idunn_widget_t *places_build(void);
/** mark the place that is open now */
void places_update(void);

// main.c

/** open a folder; history: remember the one before (back) */
void navigate(const char *path, bool history);
void go_back(void);
void go_forward(void);
void go_up(void);
/** a short message in a dialog */
void show_message(const char *title, const char *text);

// open.c

/** open an entry: a folder in place, else the program for its type */
void open_entry(const entry_t *e);
/** a program, or a file's program, in mimir (stays open after it ends) */
void open_in_mimir(const char *folder, const char *program);
/** start a program without waiting for it */
bool spawn(char *const argv[], const char *folder);

// actions.c

void actions_item_menu(void);
void actions_copy(bool cut);
void actions_paste(void);
void actions_rename(void);
void actions_new(bool folder);
void actions_trash(void);
void actions_delete(void);
void actions_restore(void);
void actions_empty_trash(void);
void actions_properties(void);
bool actions_key(idunn_window_t *win, uint16_t code, uint32_t mods, const char *text,
                 void *data);

// jobs.c

typedef enum
{
    JOB_COPY,
    JOB_MOVE,
    JOB_DELETE,
} job_kind_t;

typedef enum
{
    CONFLICT_SKIP,
    CONFLICT_KEEP_BOTH,
    CONFLICT_REPLACE,
} conflict_t;

/**
 * copy or move paths into a folder, or delete them, in a child process
 * with a progress dialog. a name that is already there asks first.
 */
void job_start(job_kind_t kind, char **paths, int count, const char *to);
bool job_running(void);

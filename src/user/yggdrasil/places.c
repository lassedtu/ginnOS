/**
 * @file places.c
 * @brief yggdrasil: the sidebar: the home folder and the folders in it,
 *        the whole computer, and the trash. the place that is open has
 *        the outline look. a right click on the trash can empty it.
 */

#include "yggdrasil.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    const char *name;
    const char *icon;    // Lucide
    const char *folder;  // in the home folder ("" for home itself), or NULL
    const char *path;    // or a whole path ("trash": the trash's files)
} place_t;

static const place_t places[] = {
    {"Home", "house", "", NULL},
    {"Desktop", "monitor", "Desktop", NULL},
    {"Documents", "file-text", "Documents", NULL},
    {"Downloads", "download", "Downloads", NULL},
    {"Pictures", "image", "Pictures", NULL},
    {"Music", "file-music", "Music", NULL},
    {"Computer", "hard-drive", NULL, "/"},
    {"Trash", "trash", NULL, "trash"},
};
#define PLACE_COUNT (int)(sizeof(places) / sizeof(places[0]))

static idunn_widget_t *buttons[PLACE_COUNT];

static void place_path(int i, char *out, size_t length)
{
    if (places[i].folder)
        snprintf(out, length, "%s%s%s", Y.home, places[i].folder[0] ? "/" : "",
                 places[i].folder);
    else if (strcmp(places[i].path, "trash") == 0)
        snprintf(out, length, "%s/files", Y.trash);
    else
        snprintf(out, length, "%s", places[i].path);
}

static void place_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    char path[PATH_LEN];
    int i = (int)(intptr_t)data;
    place_path(i, path, sizeof(path));
    navigate(path, true);
}

#define TRASH_PLACE 7

static void trash_open(idunn_widget_t *w, void *data)
{
    (void)data;
    place_clicked(w, (void *)(intptr_t)TRASH_PLACE);
}

static void trash_empty_chosen(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    actions_empty_trash();
}

/** true when the trash has something in it */
static bool trash_has_things(void)
{
    char files[PATH_LEN];
    snprintf(files, sizeof(files), "%s/files", Y.trash);
    DIR *dir = opendir(files);
    struct dirent *e;
    bool found = false;
    while (dir && !found && (e = readdir(dir)))
        found = strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0;
    if (dir)
        closedir(dir);
    return found;
}

static void trash_menu(idunn_widget_t *w, void *data)
{
    (void)data;
    static idunn_menu_t *menu;
    if (menu)
        idunn_menu_free(menu);
    menu = idunn_menu_new();
    idunn_menu_add(menu, "folder-open", "Open", NULL, trash_open, NULL);
    idunn_menu_add_separator(menu);
    int empty = idunn_menu_add(menu, "trash", "Empty Trash...", NULL, trash_empty_chosen,
                               NULL);
    idunn_menu_set_disabled(menu, empty, !trash_has_things());
    idunn_menu_popup_at_pointer(menu, w);
}

void places_update(void)
{
    char path[PATH_LEN];
    for (int i = 0; i < PLACE_COUNT; i++)
    {
        place_path(i, path, sizeof(path));
        bool open = strcmp(path, Y.folder.path) == 0;
        idunn_button_set_variant(buttons[i], open ? IDUNN_OUTLINE : IDUNN_GHOST);
    }
}

static idunn_widget_t *heading(const char *text)
{
    idunn_widget_t *label = idunn_label(text, IDUNN_TEXT_SMALL);
    idunn_label_set_color(label, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_widget_t *box = idunn_column(0, 0);
    idunn_box_set_padding(box, 2, 2);
    idunn_add(box, label);
    return box;
}

idunn_widget_t *places_build(void)
{
    idunn_widget_t *side = idunn_column(1, 3);
    idunn_set_size(side, 190, 0);
    idunn_box_set_background(side, IDUNN_COLOR_SIDEBAR);
    // a user whose home is the root (root, for now) has no places of its own
    bool own_home = strcmp(Y.home, "/") != 0;
    if (own_home)
        idunn_add(side, heading("Places"));
    for (int i = 0; i < PLACE_COUNT; i++)
    {
        if (i == 6)
            idunn_add(side, heading("System"));
        buttons[i] = idunn_add(side, idunn_button(places[i].name, IDUNN_GHOST, place_clicked,
                                                  (void *)(intptr_t)i));
        idunn_button_set_icon(buttons[i], places[i].icon);
        idunn_button_set_align(buttons[i], IDUNN_START);
        if (places[i].folder && !own_home)
            idunn_set_hidden(buttons[i], true);
    }
    idunn_on_menu(buttons[TRASH_PLACE], trash_menu, NULL);
    return side;
}

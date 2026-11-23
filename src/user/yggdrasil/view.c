/**
 * @file view.c
 * @brief yggdrasil: the open folder as a list (icon, name, size, time) or
 *        a grid of large icons, and the status line under it.
 *
 * the items of the list are the entries of Y.folder in the same order,
 * so an item's index is its entry's index.
 */

#include "yggdrasil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void activated(idunn_widget_t *list, void *data)
{
    (void)data;
    int i = idunn_list_view_current(list);
    if (i >= 0 && i < Y.folder.count)
        open_entry(&Y.folder.entries[i]);
}

static void selection_changed(idunn_widget_t *list, void *data)
{
    (void)list;
    (void)data;
    view_status();
}

/** a right click beside the items: no item is meant */
static void background_menu(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    for (int i = 0; Y.list && i < Y.folder.count; i++)
        idunn_list_view_select(Y.list, i, false);
    view_status();
    actions_item_menu();
}

static void item_menu(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    view_status();
    actions_item_menu();
}

/** "4.2 KiB  2026-11-22 14:03" for a file, the time for a folder */
static void detail(const entry_t *e, char *out, size_t length)
{
    char when[32];
    format_time(e->mtime, when, sizeof(when));
    if (S_ISDIR(e->mode))
    {
        snprintf(out, length, "%s", when);
        return;
    }
    char size[32];
    format_size((uint64_t)e->size, size, sizeof(size));
    snprintf(out, length, "%s    %s", size, when);
}

bool view_in_trash(void)
{
    char files[PATH_LEN];
    snprintf(files, sizeof(files), "%s/files", Y.trash);
    return strcmp(Y.folder.path, files) == 0;
}

void view_build(void)
{
    idunn_widget_free(Y.scroll);
    Y.scroll = NULL;
    Y.list = NULL;

    if (Y.folder.error || Y.folder.count == 0)
    {
        const char *title = Y.folder.error ? "Cannot open this folder"
                            : Y.filter[0]     ? "No names match"
                            : view_in_trash() ? "The trash is empty"
                                              : "This folder is empty";
        const char *text = Y.folder.error ? strerror(Y.folder.error)
                           : Y.filter[0]      ? "Nothing here has that in its name."
                           : view_in_trash()  ? "Things you move to the trash wait here."
                                              : "Right click to make a folder or a file.";
        const char *icon = Y.folder.error ? "circle-alert" : Y.filter[0] ? "search" : "folder-open";
        idunn_widget_t *empty = idunn_empty_state(icon, title, text);
        Y.scroll = idunn_grow(idunn_add(Y.view_holder, idunn_scroll_view(empty)), 1);
        idunn_on_menu(Y.scroll, background_menu, NULL);
        view_status();
        return;
    }

    Y.list = Y.grid ? idunn_grid_view() : idunn_list_view();
    idunn_list_view_set_multiple(Y.list, true);
    idunn_list_view_set_pictures(Y.list, true);
    char text[64];
    for (int i = 0; i < Y.folder.count; i++)
    {
        const entry_t *e = &Y.folder.entries[i];
        if (Y.grid)
            idunn_list_view_add(Y.list, e->icon, e->name, NULL);
        else
        {
            detail(e, text, sizeof(text));
            idunn_list_view_add(Y.list, e->icon, e->name, text);
        }
    }
    idunn_list_view_on_activate(Y.list, activated, NULL);
    idunn_list_view_on_select(Y.list, selection_changed, NULL);
    idunn_list_view_on_menu(Y.list, item_menu, NULL);

    idunn_widget_t *pad = idunn_column(0, 0);
    idunn_box_set_padding(pad, 2, 2);
    idunn_add(pad, Y.list);
    Y.scroll = idunn_grow(idunn_add(Y.view_holder, idunn_scroll_view(pad)), 1);
    idunn_on_menu(Y.scroll, background_menu, NULL);
    if (!toolbar_filtering())
        idunn_set_focus(Y.list); // not while a filter is typed

    view_status();
}

void view_refresh(void)
{
    folder_t fresh = {0};
    folder_read(&fresh, Y.folder.path, Y.show_hidden, Y.mime);
    folder_filter(&fresh, Y.filter);
    folder_sort(&fresh, Y.sort, Y.sort_reverse);
    if (folder_same(&fresh, &Y.folder))
    {
        folder_free(&fresh);
        return;
    }
    // keep what was selected, by name, and where the view was scrolled to
    int count = 0;
    char **names = Y.folder.count ? calloc((size_t)Y.folder.count, sizeof(char *)) : NULL;
    for (int i = 0; names && Y.list && i < Y.folder.count; i++)
        if (idunn_list_view_selected(Y.list, i))
            names[count++] = strdup(Y.folder.entries[i].name);
    int32_t offset = Y.scroll ? idunn_scroll_view_offset(Y.scroll) : 0;

    folder_free(&Y.folder);
    Y.folder = fresh;
    view_build();
    for (int i = 0; i < count; i++)
    {
        view_select_name(names[i]);
        free(names[i]);
    }
    free(names);
    if (Y.scroll)
        idunn_scroll_view_scroll_to(Y.scroll, offset);
}

int view_selection(int *out, int max)
{
    int count = 0;
    for (int i = 0; Y.list && i < Y.folder.count && count < max; i++)
        if (idunn_list_view_selected(Y.list, i))
            out[count++] = i;
    return count;
}

void view_select_name(const char *name)
{
    for (int i = 0; Y.list && i < Y.folder.count; i++)
        if (strcmp(Y.folder.entries[i].name, name) == 0)
            idunn_list_view_select(Y.list, i, true);
    view_status();
}

void view_set_grid(bool grid)
{
    if (Y.grid == grid)
        return;
    Y.grid = grid;
    int *chosen = Y.folder.count ? malloc((size_t)Y.folder.count * sizeof(int)) : NULL;
    int count = chosen ? view_selection(chosen, Y.folder.count) : 0;
    view_build();
    for (int i = 0; i < count; i++)
        idunn_list_view_select(Y.list, chosen[i], true);
    free(chosen);
    toolbar_update();
    view_status();
}

void view_status(void)
{
    if (!Y.status)
        return;
    char text[160];
    if (Y.folder.error)
    {
        snprintf(text, sizeof(text), " ");
        idunn_label_set_text(Y.status, text);
        return;
    }
    int selected = 0;
    uint64_t bytes = 0;
    int folders = 0;
    for (int i = 0; Y.list && i < Y.folder.count; i++)
    {
        if (!idunn_list_view_selected(Y.list, i))
            continue;
        selected++;
        if (S_ISDIR(Y.folder.entries[i].mode))
            folders++;
        else
            bytes += (uint64_t)Y.folder.entries[i].size;
    }
    int n = Y.folder.count;
    if (selected == 0)
        snprintf(text, sizeof(text), "%d %s", n, n == 1 ? "item" : "items");
    else if (folders == selected)
        snprintf(text, sizeof(text), "%d of %d selected", selected, n);
    else
    {
        char size[32];
        format_size(bytes, size, sizeof(size));
        snprintf(text, sizeof(text), "%d of %d selected (%s)", selected, n, size);
    }
    idunn_label_set_text(Y.status, text);
}

idunn_widget_t *view_widget(void)
{
    return Y.list ? Y.list : Y.view_holder;
}

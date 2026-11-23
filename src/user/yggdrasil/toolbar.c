/**
 * @file toolbar.c
 * @brief yggdrasil: the toolbar: back, forward, up, the path bar, and the
 *        list and grid buttons.
 *
 * the path bar has a button for each folder of the path (the home folder
 * is one button, "Home"); a click goes there. when the path is longer
 * than the bar, the first buttons go out of view on the left. Ctrl+L, or a click on the
 * free space of the bar, makes it a text field to type a path in.
 */

#include "yggdrasil.h"

#include <conf.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PARTS_MAX 64 // folders in the path bar; a long path shows its end
#define NAME_SHOWN 32 // a longer folder name is cut, with "..." at its end

static struct
{
    idunn_widget_t *back;
    idunn_widget_t *forward;
    idunn_widget_t *up;
    idunn_widget_t *bar; // the path bar: the part buttons, or the text field
    idunn_widget_t *items[2 * PARTS_MAX + 2];
    int item_count;
    char paths[PARTS_MAX + 1][PATH_LEN];
    idunn_widget_t *list_button;
    idunn_widget_t *grid_button;
    idunn_widget_t *filter;
    idunn_menu_t *sort_menu;
} T;

static void back_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    go_back();
}

static void forward_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    go_forward();
}

static void up_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    go_up();
}

static void part_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    navigate(T.paths[(intptr_t)data], true);
}

static void list_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    view_set_grid(false);
}

static void grid_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    view_set_grid(true);
}

static void clear_bar(void)
{
    for (int i = 0; i < T.item_count; i++)
        idunn_widget_free(T.items[i]);
    T.item_count = 0;
}

static void add_item(idunn_widget_t *w)
{
    if (T.item_count < (int)(sizeof(T.items) / sizeof(T.items[0])))
        T.items[T.item_count++] = idunn_add(T.bar, w);
    else
        idunn_widget_free(w);
}

static void add_part(int index, const char *icon, const char *name, bool last)
{
    if (T.item_count > 0)
        add_item(idunn_icon_view("chevron-right", 14, IDUNN_COLOR_MUTED_FOREGROUND));
    idunn_widget_t *b = idunn_button(name, last ? IDUNN_SECONDARY : IDUNN_GHOST, part_clicked,
                                     (void *)(intptr_t)index);
    idunn_button_set_size(b, IDUNN_SIZE_SMALL);
    if (icon)
        idunn_button_set_icon(b, icon);
    add_item(b);
}

/** the buttons of the path: Home or the computer, then each folder */
static void build_parts(void)
{
    const char *path = Y.folder.path;
    if (view_in_trash())
    {
        snprintf(T.paths[0], PATH_LEN, "%s", path);
        add_part(0, "trash", "Trash", true);
        return;
    }
    size_t home_len = strlen(Y.home);
    bool in_home = strncmp(path, Y.home, home_len) == 0 &&
                   (path[home_len] == '\0' || path[home_len] == '/') && home_len > 1;
    const char *rest = in_home ? path + home_len : path;

    // the start of each folder name after the first button
    const char *starts[64];
    int count = 0;
    for (const char *p = rest; *p && count < 64; p++)
        if (*p == '/' && p[1] && p[1] != '/')
            starts[count++] = p + 1;

    int first = count > PARTS_MAX - 1 ? count - (PARTS_MAX - 1) : 0;
    int index = 0;
    snprintf(T.paths[index], PATH_LEN, "%s", in_home ? Y.home : "/");
    add_part(index++, in_home ? "house" : "hard-drive", in_home ? "Home" : "Computer",
             count == 0);
    for (int i = first; i < count; i++)
    {
        const char *end = i + 1 < count ? starts[i + 1] - 1 : rest + strlen(rest);
        snprintf(T.paths[index], PATH_LEN, "%.*s", (int)(end - path), path);
        int length = (int)(end - starts[i]);
        char name[NAME_SHOWN + 8];
        if (length > NAME_SHOWN)
            snprintf(name, sizeof(name), "%.*s...", NAME_SHOWN - 3, starts[i]);
        else
            snprintf(name, sizeof(name), "%.*s", length, starts[i]);
        add_part(index++, NULL, name, i == count - 1);
    }
}

static void path_typed(idunn_widget_t *field, void *data)
{
    (void)data;
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s", idunn_text_field_text(field));
    if (path[0] == '~')
    {
        char rest[PATH_LEN];
        snprintf(rest, sizeof(rest), "%s", path + 1);
        snprintf(path, sizeof(path), "%s%s", Y.home, rest);
    }
    if (path[0] != '/')
    {
        show_message("Invalid path", "Paths must start with / or ~.");
        return;
    }
    navigate(path, true); // it builds the bar again
}

void toolbar_edit_path(void)
{
    clear_bar();
    idunn_widget_t *field = idunn_text_field("A folder, such as /usr/share");
    idunn_text_field_set_text(field, Y.folder.path);
    idunn_text_field_on_submit(field, path_typed, NULL);
    add_item(idunn_grow(field, 1));
    idunn_set_focus(field);
}

static void bar_menu(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    toolbar_edit_path();
}

void toolbar_update(void)
{
    clear_bar();
    build_parts();
    idunn_set_disabled(T.back, Y.back_count == 0);
    idunn_set_disabled(T.forward, Y.forward_count == 0);
    idunn_set_disabled(T.up, strcmp(Y.folder.path, "/") == 0);
    idunn_button_set_variant(T.list_button, Y.grid ? IDUNN_GHOST : IDUNN_SECONDARY);
    idunn_button_set_variant(T.grid_button, Y.grid ? IDUNN_SECONDARY : IDUNN_GHOST);
}

static void filter_typed(idunn_widget_t *field, void *data)
{
    (void)data;
    snprintf(Y.filter, sizeof(Y.filter), "%s", idunn_text_field_text(field));
    view_refresh();
}

/** Enter in the filter: to the list, the first match current */
static void filter_submitted(idunn_widget_t *field, void *data)
{
    (void)field;
    (void)data;
    if (Y.list)
        idunn_set_focus(Y.list);
}

bool toolbar_filtering(void)
{
    return T.filter && idunn_window_focus(Y.win) == T.filter;
}

void toolbar_find(void)
{
    idunn_set_focus(T.filter);
    idunn_text_field_select(T.filter, 0, (size_t)-1);
}

bool toolbar_clear_filter(void)
{
    if (!Y.filter[0])
        return false;
    Y.filter[0] = '\0';
    idunn_text_field_set_text(T.filter, "");
    return true;
}

static const char *const sort_names[] = {"name", "size", "type", "date"};
#define SORT_ITEMS 4 // the menu's items before its separator

/** the checks of the sort menu as the order is */
static void show_sort(void)
{
    for (int i = 0; i < SORT_ITEMS; i++)
        idunn_menu_set_checked(T.sort_menu, i, (int)Y.sort == i);
    idunn_menu_set_checked(T.sort_menu, SORT_ITEMS + 1, Y.sort_reverse);
}

/** the order is kept in yggdrasil.conf for the next time */
static void save_sort(void)
{
    conf_t *conf = conf_open("yggdrasil");
    if (!conf)
        return;
    conf_set(conf, "view", "sort", sort_names[Y.sort]);
    conf_set(conf, "view", "reverse", Y.sort_reverse ? "yes" : "no");
    conf_save(conf);
    conf_free(conf);
}

void toolbar_set_sort(sort_t by, bool reverse)
{
    Y.sort = by;
    Y.sort_reverse = reverse;
    show_sort();
    save_sort();
    view_refresh();
}

static void sort_chosen(idunn_widget_t *w, void *data)
{
    (void)w;
    int what = (int)(intptr_t)data;
    if (what < SORT_ITEMS)
        Y.sort = (sort_t)what;
    else
        Y.sort_reverse = !Y.sort_reverse;
    show_sort();
    save_sort();
    view_refresh();
}

static void sort_clicked(idunn_widget_t *b, void *data)
{
    (void)data;
    idunn_menu_popup(T.sort_menu, b);
}

void toolbar_load_sort(void)
{
    conf_t *conf = conf_open("yggdrasil");
    const char *by = conf ? conf_get(conf, "view", "sort", "name") : "name";
    for (int i = 0; i < SORT_ITEMS; i++)
        if (strcmp(by, sort_names[i]) == 0)
            Y.sort = (sort_t)i;
    const char *reverse = conf ? conf_get(conf, "view", "reverse", "no") : "no";
    Y.sort_reverse = strcmp(reverse, "yes") == 0;
    conf_free(conf);
}

idunn_widget_t *toolbar_build(void)
{
    idunn_widget_t *row = idunn_row(1, 0);
    idunn_box_set_padding(row, 2, 3);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
    T.back = idunn_add(row, idunn_icon_button("arrow-left", IDUNN_GHOST, back_clicked, NULL));
    idunn_set_tooltip(T.back, "Back (Alt+Left)");
    T.forward = idunn_add(row, idunn_icon_button("arrow-right", IDUNN_GHOST, forward_clicked,
                                                 NULL));
    idunn_set_tooltip(T.forward, "Forward (Alt+Right)");
    T.up = idunn_add(row, idunn_icon_button("arrow-up", IDUNN_GHOST, up_clicked, NULL));
    idunn_set_tooltip(T.up, "Parent folder (Backspace)");

    T.bar = idunn_grow(idunn_add(row, idunn_row(1, 0)), 1);
    idunn_box_set_padding(T.bar, 0, 2);
    idunn_box_set_keep_end(T.bar, true);
    idunn_box_set_align(T.bar, IDUNN_CENTER, IDUNN_START);
    idunn_on_menu(T.bar, bar_menu, NULL);

    T.filter = idunn_add(row, idunn_text_field("Filter"));
    idunn_text_field_set_icon(T.filter, "search");
    idunn_text_field_on_change(T.filter, filter_typed, NULL);
    idunn_text_field_on_submit(T.filter, filter_submitted, NULL);
    idunn_set_size(T.filter, 180, 0);
    idunn_set_tooltip(T.filter, "Filter by name (Ctrl+F)");

    T.sort_menu = idunn_menu_new();
    idunn_menu_add_check(T.sort_menu, "Name", false, sort_chosen, (void *)0);
    idunn_menu_add_check(T.sort_menu, "Size", false, sort_chosen, (void *)1);
    idunn_menu_add_check(T.sort_menu, "Type", false, sort_chosen, (void *)2);
    idunn_menu_add_check(T.sort_menu, "Date modified", false, sort_chosen, (void *)3);
    idunn_menu_add_separator(T.sort_menu);
    idunn_menu_add_check(T.sort_menu, "Reverse order", false, sort_chosen, (void *)4);
    show_sort();
    idunn_widget_t *sort = idunn_add(row, idunn_icon_button("arrow-down-wide-narrow", IDUNN_GHOST,
                                                            sort_clicked, NULL));
    idunn_set_tooltip(sort, "Sort");

    T.list_button = idunn_add(row, idunn_icon_button("list", IDUNN_SECONDARY, list_clicked,
                                                     NULL));
    idunn_set_tooltip(T.list_button, "List");
    T.grid_button = idunn_add(row, idunn_icon_button("layout-grid", IDUNN_GHOST, grid_clicked,
                                                     NULL));
    idunn_set_tooltip(T.grid_button, "Icons");
    return row;
}

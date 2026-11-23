/**
 * @file main.c
 * @brief settings: the window, with the list of pages on the left and
 * the page on the right.
 *
 * usage: settings [page]   (appearance, display, keyboard, sound, terminal, about)
 *
 * each value lives in a plain config file (libconf, ~/.config/ginnos),
 * so it works without this app too. after a change the app tells the
 * window server, and every open window follows at once: the theme
 * (theme.conf), the wallpaper (desktop.conf), the keyboard layout
 * (keyboard.conf), mimir's text size (mimir.conf).
 */

#include "settings.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    const char *id; // for "settings <page>"
    const char *name;
    const char *icon;
    const char *description;
    const char *words; // more that the search finds it by
    idunn_widget_t *(*build)(void);
} page_t;

static const page_t pages[] = {
    {"appearance", "Appearance", "palette", "Theme, accent colour and wallpaper",
     "dark light mode color colour accent wallpaper background picture text size font scale "
     "large small",
     appearance_page},
    {"display", "Display", "monitor", "Screen resolution",
     "screen resolution size monitor pixels mode", display_page},
    {"keyboard", "Keyboard", "keyboard", "Keyboard layout",
     "layout keys language danish english us dk", keyboard_page},
    {"sound", "Sound", "volume-2", "Output volume",
     "volume audio mixer speaker loud quiet mute", sound_page},
    {"terminal", "Terminal", "square-terminal", "Font and text size for Mimir",
     "mimir console shell font text size", terminal_page},
    {"about", "About", "info", "System information",
     "version system kernel memory cpu processor computer ginnos", about_page},
};
#define PAGE_COUNT (int)(sizeof(pages) / sizeof(pages[0]))

static struct
{
    idunn_widget_t *nav[PAGE_COUNT];
    idunn_widget_t *nothing; // "No settings match"
    idunn_widget_t *main;
    idunn_widget_t *title;
    idunn_widget_t *description;
    idunn_widget_t *page;
} M;

static idunn_widget_t *page_view(int index)
{
    idunn_widget_t *content = pages[index].build();
    idunn_box_set_padding(content, 6, 8);
    return idunn_scroll_view(content);
}

static void show_page(idunn_widget_t *button, void *data)
{
    (void)button;
    int index = (int)(intptr_t)data;
    for (int i = 0; i < PAGE_COUNT; i++)
        idunn_button_set_variant(M.nav[i], i == index ? IDUNN_OUTLINE : IDUNN_GHOST);
    idunn_label_set_text(M.title, pages[index].name);
    idunn_label_set_text(M.description, pages[index].description);
    idunn_widget_free(M.page);
    M.page = idunn_grow(idunn_add(M.main, page_view(index)), 1);
}

/** true when text has part in it, with no regard to letter case */
static bool has(const char *text, const char *part)
{
    size_t n = strlen(part);
    for (const char *t = text; *t; t++)
        if (strncasecmp(t, part, n) == 0)
            return true;
    return n == 0;
}

static bool page_matches(int i, const char *search)
{
    return has(pages[i].name, search) || has(pages[i].description, search) ||
           has(pages[i].words, search);
}

/** the pages that do not match are hidden from the side bar */
static void search_typed(idunn_widget_t *field, void *data)
{
    (void)data;
    const char *search = idunn_text_field_text(field);
    int shown = 0;
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        bool match = page_matches(i, search);
        idunn_set_hidden(M.nav[i], !match);
        shown += match;
    }
    idunn_set_hidden(M.nothing, shown > 0);
}

/** Enter: the first page that matches */
static void search_submitted(idunn_widget_t *field, void *data)
{
    (void)data;
    const char *search = idunn_text_field_text(field);
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        if (page_matches(i, search))
        {
            show_page(NULL, (void *)(intptr_t)i);
            return;
        }
    }
}

static idunn_widget_t *side_bar(int first)
{
    idunn_widget_t *side = idunn_column(1, 3);
    idunn_set_size(side, 200, 0);
    idunn_box_set_background(side, IDUNN_COLOR_SIDEBAR);
    idunn_widget_t *brand = idunn_add(side, idunn_column(0, 0));
    idunn_box_set_padding(brand, 2, 2);
    idunn_add(brand, idunn_label("Settings", IDUNN_TEXT_TITLE));
    idunn_widget_t *search = idunn_add(side, idunn_text_field("Search"));
    idunn_text_field_set_icon(search, "search");
    idunn_text_field_on_change(search, search_typed, NULL);
    idunn_text_field_on_submit(search, search_submitted, NULL);
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        M.nav[i] = idunn_add(side, idunn_button(pages[i].name,
                                                i == first ? IDUNN_OUTLINE : IDUNN_GHOST,
                                                show_page, (void *)(intptr_t)i));
        idunn_button_set_icon(M.nav[i], pages[i].icon);
        idunn_button_set_align(M.nav[i], IDUNN_START);
    }
    M.nothing = idunn_add(side, idunn_label("No settings match.", IDUNN_TEXT_SMALL));
    idunn_label_set_color(M.nothing, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_set_hidden(M.nothing, true);
    return side;
}

int main(int argc, char **argv)
{
    int first = 0;
    if (argc > 1)
    {
        first = -1;
        for (int i = 0; i < PAGE_COUNT; i++)
            if (strcmp(argv[1], pages[i].id) == 0)
                first = i;
        if (first < 0)
        {
            fprintf(stderr, "usage: settings [appearance|display|keyboard|sound|terminal|about]\n");
            return 1;
        }
    }
    settings_app = idunn_app_new("settings");
    if (!settings_app)
    {
        printf("settings: no window server (start the desktop with startde)\n");
        return 1;
    }
    settings_conn = idunn_app_conn(settings_app);
    idunn_window_t *win = idunn_window_new(settings_app, "Settings", 820, 600);
    if (!win)
        return 1;

    idunn_widget_t *root = idunn_row(0, 0);
    idunn_add(root, side_bar(first));
    idunn_add(root, idunn_separator());
    M.main = idunn_grow(idunn_add(root, idunn_column(0, 0)), 1);
    idunn_widget_t *head = idunn_add(M.main, idunn_column(1, 0));
    idunn_box_set_padding(head, 5, 8);
    M.title = idunn_add(head, idunn_label(pages[first].name, IDUNN_TEXT_HEADING));
    M.description = idunn_add(head, muted(pages[first].description, IDUNN_TEXT_BODY));
    idunn_add(M.main, idunn_separator());
    M.page = idunn_grow(idunn_add(M.main, page_view(first)), 1);

    idunn_window_set_root(win, root);
    return idunn_app_run(settings_app);
}

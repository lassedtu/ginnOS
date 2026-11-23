/**
 * @file main.c
 * @brief settings: the window, with the list of pages on the left and
 * the page on the right.
 *
 * usage: settings [page]   (appearance, keyboard, sound, terminal, about)
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
    idunn_widget_t *(*build)(void);
} page_t;

static const page_t pages[] = {
    {"appearance", "Appearance", "palette", "The theme, the accent colour and the wallpaper.",
     appearance_page},
    {"keyboard", "Keyboard", "keyboard", "The layout the keys type in.", keyboard_page},
    {"sound", "Sound", "volume-2", "The volume of the sound card.", sound_page},
    {"terminal", "Terminal", "square-terminal", "The text of mimir.", terminal_page},
    {"about", "About", "info", "What this computer is and runs.", about_page},
};
#define PAGE_COUNT (int)(sizeof(pages) / sizeof(pages[0]))

static struct
{
    idunn_widget_t *nav[PAGE_COUNT];
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

static idunn_widget_t *side_bar(int first)
{
    idunn_widget_t *side = idunn_column(1, 3);
    idunn_set_size(side, 200, 0);
    idunn_box_set_background(side, IDUNN_COLOR_SIDEBAR);
    idunn_widget_t *brand = idunn_add(side, idunn_column(0, 0));
    idunn_box_set_padding(brand, 2, 2);
    idunn_add(brand, idunn_label("Settings", IDUNN_TEXT_TITLE));
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        M.nav[i] = idunn_add(side, idunn_button(pages[i].name,
                                                i == first ? IDUNN_OUTLINE : IDUNN_GHOST,
                                                show_page, (void *)(intptr_t)i));
        idunn_button_set_icon(M.nav[i], pages[i].icon);
        idunn_button_set_align(M.nav[i], IDUNN_START);
    }
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
            fprintf(stderr, "usage: settings [appearance|keyboard|sound|terminal|about]\n");
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

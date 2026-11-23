/**
 * @file terminal.c
 * @brief settings: mimir's text size (mimir.conf).
 */

#include "settings.h"

static const char *const term_sizes[] = {"11", "12", "13", "14", "15", "16", "18", "20"};

static void term_size_chosen(idunn_widget_t *select, void *data)
{
    (void)data;
    save_setting("mimir", "terminal", "font_size", term_sizes[idunn_select_chosen(select)]);
}

idunn_widget_t *terminal_page(void)
{
    idunn_widget_t *page = idunn_column(6, 0);
    idunn_widget_t *card = page_card(page, "Text", "Open terminals change at once.");
    char value[16];
    read_setting("mimir", "terminal", "font_size", "13", value, sizeof(value));
    int count = (int)(sizeof(term_sizes) / sizeof(term_sizes[0]));
    int chosen = index_of(term_sizes, count, value);
    idunn_add(card, idunn_field("Text size",
                                idunn_select(term_sizes, count, chosen < 0 ? 2 : chosen,
                                             term_size_chosen, NULL),
                                "In pixels at 1x. Ctrl+plus and Ctrl+minus change it in one "
                                "window, Ctrl+0 goes back to this size."));
    return page;
}

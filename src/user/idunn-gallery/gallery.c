/**
 * @file gallery.c
 * @brief idunn-gallery: every idunn component in all its states, in the
 *        light and the dark theme. the test bed of idunn-ui and the place
 *        to review its look.
 */

#include <idunn.h>
#include <stdio.h>

typedef struct
{
    const char *name;
    const char *icon;
    const char *description;
    idunn_widget_t *(*build)(void);
} page_t;

static idunn_widget_t *buttons_page(void);
static idunn_widget_t *text_page(void);

static const page_t pages[] = {
    {"Buttons", "layout-grid", "Actions in six variants, three sizes, with icons and without.",
     buttons_page},
    {"Text", "type", "The text styles: Inter in three weights, and JetBrains Mono.", text_page},
};
#define PAGE_COUNT (int)(sizeof(pages) / sizeof(pages[0]))

static struct
{
    idunn_app_t *app;
    idunn_widget_t *main;
    idunn_widget_t *page;
    idunn_widget_t *title;
    idunn_widget_t *description;
    idunn_widget_t *nav[PAGE_COUNT];
    idunn_widget_t *mode;
    idunn_widget_t *status;
} G;

static void clicked(idunn_widget_t *button, void *data)
{
    (void)button;
    char text[96];
    snprintf(text, sizeof(text), "You clicked %s.", (const char *)data);
    if (G.status)
        idunn_label_set_text(G.status, text);
}

static idunn_widget_t *muted(const char *text, idunn_text_t style)
{
    idunn_widget_t *label = idunn_label(text, style);
    idunn_label_set_color(label, IDUNN_COLOR_MUTED_FOREGROUND);
    return label;
}

/**
 * a part of a page: a title, a line about it, and its content.
 */
static idunn_widget_t *section(idunn_widget_t *page, const char *title, const char *about,
                               idunn_widget_t *content)
{
    idunn_widget_t *col = idunn_add(page, idunn_column(1, 0));
    idunn_add(col, idunn_label(title, IDUNN_TEXT_LABEL));
    idunn_add(col, muted(about, IDUNN_TEXT_SMALL));
    idunn_widget_t *space = idunn_add(col, idunn_spacer());
    idunn_grow(space, 0);
    idunn_set_size(space, 0, 8);
    idunn_add(col, content);
    return col;
}

static idunn_widget_t *button_row(void)
{
    idunn_widget_t *row = idunn_row(2, 0);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
    return row;
}

static idunn_widget_t *add_button(idunn_widget_t *row, const char *text, idunn_variant_t variant)
{
    return idunn_add(row, idunn_button(text, variant, clicked, (void *)text));
}

static idunn_widget_t *buttons_page(void)
{
    idunn_widget_t *page = idunn_column(6, 0);

    idunn_widget_t *row = button_row();
    add_button(row, "Primary", IDUNN_PRIMARY);
    add_button(row, "Secondary", IDUNN_SECONDARY);
    add_button(row, "Outline", IDUNN_OUTLINE);
    add_button(row, "Ghost", IDUNN_GHOST);
    add_button(row, "Delete", IDUNN_DESTRUCTIVE);
    add_button(row, "Link", IDUNN_LINK);
    section(page, "Variants", "The main action is the accent; delete is soft red.", row);

    row = button_row();
    static const char *const sizes[] = {"Small", "Default", "Large"};
    for (int i = 0; i < 3; i++)
    {
        idunn_widget_t *b = add_button(row, sizes[i], IDUNN_PRIMARY);
        idunn_button_set_size(b, (idunn_size_t)(i == 0 ? IDUNN_SIZE_SMALL
                                                : i == 1 ? IDUNN_SIZE_DEFAULT
                                                         : IDUNN_SIZE_LARGE));
        b = add_button(row, sizes[i], IDUNN_OUTLINE);
        idunn_button_set_size(b, (idunn_size_t)(i == 0 ? IDUNN_SIZE_SMALL
                                                : i == 1 ? IDUNN_SIZE_DEFAULT
                                                         : IDUNN_SIZE_LARGE));
    }
    section(page, "Sizes", "28, 32 and 36 pixels high.", row);

    row = button_row();
    idunn_button_set_icon(add_button(row, "New file", IDUNN_PRIMARY), "plus");
    idunn_button_set_icon(add_button(row, "Download", IDUNN_OUTLINE), "download");
    idunn_button_set_icon(add_button(row, "Send", IDUNN_SECONDARY), "mail");
    idunn_button_set_icon(add_button(row, "Delete", IDUNN_DESTRUCTIVE), "trash");
    section(page, "With an icon", "A 16 pixel Lucide icon before the text.", row);

    row = button_row();
    idunn_add(row, idunn_icon_button("settings", IDUNN_OUTLINE, clicked, "Settings"));
    idunn_add(row, idunn_icon_button("search", IDUNN_GHOST, clicked, "Search"));
    idunn_add(row, idunn_icon_button("star", IDUNN_GHOST, clicked, "Star"));
    idunn_add(row, idunn_icon_button("copy", IDUNN_SECONDARY, clicked, "Copy"));
    idunn_add(row, idunn_icon_button("plus", IDUNN_PRIMARY, clicked, "Add"));
    section(page, "Icon only", "Square buttons for toolbars.", row);

    row = button_row();
    static const idunn_variant_t variants[] = {IDUNN_PRIMARY, IDUNN_SECONDARY, IDUNN_OUTLINE,
                                               IDUNN_GHOST, IDUNN_DESTRUCTIVE, IDUNN_LINK};
    static const char *const names[] = {"Primary", "Secondary", "Outline",
                                        "Ghost", "Delete", "Link"};
    for (int i = 0; i < 6; i++)
        idunn_set_disabled(add_button(row, names[i], variants[i]), true);
    section(page, "Disabled", "Half opacity, no hover, no focus.", row);

    idunn_widget_t *rule = idunn_add(page, idunn_separator());
    (void)rule;
    G.status = idunn_add(page, muted("Click a button, or use Tab and Enter.", IDUNN_TEXT_BODY));
    return page;
}

static idunn_widget_t *text_page(void)
{
    static const struct
    {
        idunn_text_t style;
        const char *name;
        const char *spec;
    } styles[] = {
        {IDUNN_TEXT_DISPLAY, "Display", "23 / 30 SemiBold"},
        {IDUNN_TEXT_HEADING, "Heading", "19 / 26 SemiBold"},
        {IDUNN_TEXT_TITLE, "Title", "15 / 22 SemiBold"},
        {IDUNN_TEXT_BODY, "Body", "13 / 18 Regular"},
        {IDUNN_TEXT_LABEL, "Label", "13 / 18 Medium"},
        {IDUNN_TEXT_SMALL, "Small", "12 / 16 Regular"},
        {IDUNN_TEXT_MONO, "Mono", "12 / 18 JetBrains Mono"},
    };
    idunn_widget_t *page = idunn_column(5, 0);
    for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); i++)
    {
        idunn_widget_t *row = idunn_add(page, idunn_row(4, 0));
        idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
        idunn_widget_t *about = idunn_add(row, idunn_column(0, 0));
        idunn_set_size(about, 140, 0);
        idunn_add(about, idunn_label(styles[i].name, IDUNN_TEXT_LABEL));
        idunn_add(about, muted(styles[i].spec, IDUNN_TEXT_SMALL));
        idunn_grow(idunn_add(row, idunn_label("The quick brown fox jumps over the lazy dog",
                                              styles[i].style)),
                   1);
    }
    idunn_widget_t *para = idunn_add(
        page, muted("Long text wraps at spaces. idunn draws Inter with bragi, at quarter pixel "
                    "places, blended as a browser does, so it looks the same as in the preview.",
                    IDUNN_TEXT_BODY));
    idunn_label_set_wrap(para, true);
    return page;
}

static void show_page(idunn_widget_t *button, void *data)
{
    (void)button;
    int index = (int)(intptr_t)data;
    for (int i = 0; i < PAGE_COUNT; i++)
        idunn_button_set_variant(G.nav[i], i == index ? IDUNN_OUTLINE : IDUNN_GHOST);
    idunn_label_set_text(G.title, pages[index].name);
    idunn_label_set_text(G.description, pages[index].description);
    G.status = NULL;
    idunn_widget_free(G.page);
    G.page = idunn_grow(idunn_add(G.main, pages[index].build()), 1);
    idunn_box_set_padding(G.page, 6, 8);
}

static void switch_mode(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_theme_t theme = *idunn_app_theme(G.app);
    theme.mode = theme.mode == IDUNN_DARK ? IDUNN_LIGHT : IDUNN_DARK;
    idunn_button_set_icon(button, theme.mode == IDUNN_DARK ? "sun" : "moon");
    idunn_app_set_theme(G.app, &theme);
}

static idunn_widget_t *side_bar(void)
{
    idunn_widget_t *side = idunn_column(1, 3);
    idunn_set_size(side, 200, 0);
    idunn_box_set_background(side, IDUNN_COLOR_SIDEBAR);
    idunn_widget_t *brand = idunn_add(side, idunn_column(0, 0));
    idunn_box_set_padding(brand, 2, 2);
    idunn_add(brand, idunn_label("idunn", IDUNN_TEXT_TITLE));
    idunn_add(brand, muted("Components", IDUNN_TEXT_SMALL));
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        G.nav[i] = idunn_add(side, idunn_button(pages[i].name, i == 0 ? IDUNN_OUTLINE : IDUNN_GHOST,
                                                show_page, (void *)(intptr_t)i));
        idunn_button_set_icon(G.nav[i], pages[i].icon);
        idunn_button_set_align(G.nav[i], IDUNN_START);
    }
    return side;
}

static idunn_widget_t *header(void)
{
    idunn_widget_t *bar = idunn_row(2, 0);
    idunn_box_set_padding(bar, 5, 8);
    idunn_box_set_align(bar, IDUNN_CENTER, IDUNN_START);
    idunn_widget_t *words = idunn_grow(idunn_add(bar, idunn_column(1, 0)), 1);
    G.title = idunn_add(words, idunn_label(pages[0].name, IDUNN_TEXT_HEADING));
    G.description = idunn_add(words, muted(pages[0].description, IDUNN_TEXT_BODY));
    bool dark = idunn_app_theme(G.app)->mode == IDUNN_DARK;
    G.mode = idunn_add(bar, idunn_icon_button(dark ? "sun" : "moon", IDUNN_OUTLINE, switch_mode,
                                              NULL));
    return bar;
}

int main(void)
{
    G.app = idunn_app_new("Gallery");
    if (!G.app)
    {
        printf("idunn-gallery: no window server (start heimdalld)\n");
        return 1;
    }
    idunn_window_t *win = idunn_window_new(G.app, "idunn Gallery", 880, 720);
    if (!win)
        return 1;

    idunn_widget_t *root = idunn_row(0, 0);
    idunn_add(root, side_bar());
    idunn_add(root, idunn_separator());
    G.main = idunn_grow(idunn_add(root, idunn_column(0, 0)), 1);
    idunn_add(G.main, header());
    idunn_add(G.main, idunn_separator());
    G.page = idunn_grow(idunn_add(G.main, pages[0].build()), 1);
    idunn_box_set_padding(G.page, 6, 8);

    idunn_window_set_root(win, root);
    return idunn_app_run(G.app);
}

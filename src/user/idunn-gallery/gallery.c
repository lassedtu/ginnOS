/**
 * @file gallery.c
 * @brief idunn-gallery: every idunn component in all its states, in the
 *        light and the dark theme. the test bed of idunn-ui and the place
 *        to review its look.
 */

#include <idunn.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    const char *name;
    const char *icon;
    const char *description;
    idunn_widget_t *(*build)(void);
} page_t;

static idunn_widget_t *buttons_page(void);
static idunn_widget_t *inputs_page(void);
static idunn_widget_t *lists_page(void);
static idunn_widget_t *cards_page(void);
static idunn_widget_t *menus_page(void);
static idunn_widget_t *dialogs_page(void);
static idunn_widget_t *text_page(void);

static const page_t pages[] = {
    {"Buttons", "layout-grid", "Actions in six variants, three sizes, with icons and without.",
     buttons_page},
    {"Inputs", "pencil", "Text fields, choices and sliders, with help and errors.", inputs_page},
    {"Lists", "list", "Lists and icon grids that scroll, with one or many selected.", lists_page},
    {"Cards", "panels-top-left", "Cards, tabs, badges and empty states.", cards_page},
    {"Menus", "menu", "Menus, selects, tooltips and popovers, in popup windows.", menus_page},
    {"Dialogs", "square", "Dialogs over the window, for questions and short forms.", dialogs_page},
    {"Text", "type", "The text styles: Inter in three weights, and JetBrains Mono.", text_page},
};
#define PAGE_COUNT (int)(sizeof(pages) / sizeof(pages[0]))

static struct
{
    idunn_app_t *app;
    idunn_menu_t *file_menu;
    idunn_menu_t *more_menu;
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

static void check_email(idunn_widget_t *input, void *data)
{
    idunn_widget_t *field = data;
    const char *text = idunn_text_field_text(input);
    idunn_field_set_error(field, *text && !strchr(text, '@') ? "An email address has an @ in it."
                                                             : NULL);
}

static void show_speed(idunn_widget_t *slider, void *data)
{
    char text[32];
    snprintf(text, sizeof(text), "%d", (int)idunn_slider_value(slider));
    idunn_label_set_text(data, text);
}

static idunn_widget_t *inputs_page(void)
{
    idunn_widget_t *page = idunn_row(8, 0);

    idunn_widget_t *left = idunn_grow(idunn_add(page, idunn_column(5, 0)), 1);
    idunn_add(left, idunn_field("Name", idunn_text_field("Ada Lovelace"),
                                "As it is shown to other people."));
    idunn_widget_t *email = idunn_text_field("you@example.com");
    idunn_widget_t *email_field = idunn_add(left, idunn_field("Email", email, NULL));
    idunn_text_field_on_change(email, check_email, email_field);
    idunn_widget_t *search = idunn_text_field("Search files");
    idunn_text_field_set_icon(search, "search");
    idunn_add(left, idunn_field("Search", search, NULL));
    idunn_add(left, idunn_field("Notes", idunn_text_area("Write something...", 4),
                                "Enter makes a new line."));
    idunn_widget_t *off = idunn_text_field("Not now");
    idunn_set_disabled(off, true);
    idunn_add(left, idunn_field("Disabled", off, NULL));

    idunn_widget_t *right = idunn_grow(idunn_add(page, idunn_column(6, 0)), 1);
    idunn_widget_t *checks = idunn_column(2, 0);
    idunn_box_set_align(checks, IDUNN_START, IDUNN_START);
    idunn_add(checks, idunn_check_box("Show hidden files", true, NULL, NULL));
    idunn_add(checks, idunn_check_box("Ask before deleting", false, NULL, NULL));
    idunn_check_box_set_state(idunn_add(checks, idunn_check_box("Some selected", false, NULL, NULL)),
                              IDUNN_MIXED);
    section(right, "Check boxes", "Space or a click; a dash for some.", checks);

    static const char *const sizes[] = {"Small", "Medium", "Large"};
    idunn_widget_t *radios = idunn_radio_group(sizes, 3, 1, NULL, NULL);
    idunn_box_set_align(radios, IDUNN_START, IDUNN_START);
    section(right, "Radio group", "One Tab stop; the arrows choose.", radios);
    static const char *const scales[] = {"1x", "2x"};
    idunn_widget_t *row_radios = idunn_radio_group(scales, 2, 0, NULL, NULL);
    idunn_radio_group_set_row(row_radios, true);
    idunn_widget_t *in_row = idunn_row(0, 0);
    idunn_add(in_row, row_radios);
    idunn_add(right, in_row);

    idunn_widget_t *switches = idunn_column(2, 0);
    idunn_box_set_align(switches, IDUNN_START, IDUNN_START);
    idunn_add(switches, idunn_switch("Natural scrolling", true, NULL, NULL));
    idunn_add(switches, idunn_switch("Reduce motion", false, NULL, NULL));
    section(right, "Switches", "They slide in 120 ms.", switches);

    idunn_widget_t *speed = idunn_row(3, 0);
    idunn_box_set_align(speed, IDUNN_CENTER, IDUNN_START);
    idunn_widget_t *value = idunn_label("6", IDUNN_TEXT_LABEL);
    idunn_grow(idunn_add(speed, idunn_slider(0, 10, 1, 6, show_speed, value)), 1);
    idunn_set_size(idunn_add(speed, value), 20, 0);
    section(right, "Slider", "Drag it, or use the arrows and Page Up/Down.", speed);
    return page;
}

static void show_selected(idunn_widget_t *list, void *data)
{
    int count = 0;
    for (int i = 0; i < idunn_list_view_count(list); i++)
        count += idunn_list_view_selected(list, i);
    char text[64];
    snprintf(text, sizeof(text), count == 1 ? "1 file selected." : "%d files selected.", count);
    idunn_label_set_text(data, text);
}

/** a card around a scroll view of a fixed height: a frame that stays */
static idunn_widget_t *framed(idunn_widget_t *content, int32_t height)
{
    idunn_widget_t *card = idunn_card(0);
    idunn_box_set_padding(card, 1, 1);
    idunn_widget_t *view = idunn_add(card, idunn_scroll_view(content));
    idunn_set_size(view, 0, height);
    return card;
}

static idunn_widget_t *lists_page(void)
{
    idunn_widget_t *page = idunn_column(6, 0);

    idunn_widget_t *list = idunn_list_view();
    idunn_list_view_set_multiple(list, true);
    idunn_list_view_add(list, "folder", "Pictures", "12 items");
    idunn_list_view_add(list, "folder", "Documents", "48 items");
    idunn_list_view_add(list, "file-text", "notes.txt", "2.1 KB");
    idunn_list_view_add(list, "image", "wallpaper.png", "840 KB");
    for (int i = 1; i <= 200; i++)
    {
        char name[32];
        char size[16];
        snprintf(name, sizeof(name), "report-%03d.txt", i);
        snprintf(size, sizeof(size), "%d KB", (i * 37) % 900 + 3);
        idunn_list_view_add(list, "file", name, size);
    }
    idunn_widget_t *list_part = idunn_column(2, 0);
    idunn_add(list_part, framed(list, 232));
    idunn_widget_t *status = idunn_add(list_part, muted("No file selected.", IDUNN_TEXT_SMALL));
    idunn_list_view_on_select(list, show_selected, status);
    section(page, "List view", "204 rows. Ctrl and Shift select more; type a name to find it.",
            list_part);

    static const char *const files[][2] = {
        {"folder-documents", "Documents"},  {"folder-download", "Downloads"},
        {"folder-pictures", "Pictures"},    {"folder-music", "Music"},
        {"user-desktop", "Desktop"},        {"text-x-generic", "notes.txt"},
        {"text-x-script", "build.sh"},      {"text-x-csrc", "main.c"},
        {"image-x-generic", "wallpaper.png"}, {"audio-x-generic", "song.ogg"},
        {"application-x-executable", "doom"}, {"application-x-archive", "backup.tar"},
        {"user-trash", "Trash"},            {"drive-harddisk", "Disk"},
    };
    idunn_widget_t *grid = idunn_grid_view();
    idunn_list_view_set_multiple(grid, true);
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
        idunn_list_view_add(grid, files[i][0], files[i][1], NULL);
    section(page, "Grid view", "Tela icons for files; the arrows move in two directions.",
            framed(grid, 200));
    return page;
}

static idunn_widget_t *cards_page(void)
{
    idunn_widget_t *page = idunn_column(6, 0);

    idunn_widget_t *card = idunn_card(5);
    idunn_card_header(card, "Storage", "How the disk is used by your files and the system.");
    idunn_widget_t *line = idunn_add(card, idunn_row(2, 0));
    idunn_box_set_align(line, IDUNN_CENTER, IDUNN_START);
    idunn_add(line, idunn_badge("64 %", IDUNN_BADGE_SECONDARY));
    idunn_add(line, muted("2.1 GB of 3.2 GB used", IDUNN_TEXT_BODY));
    idunn_widget_t *actions = idunn_add(card, idunn_row(2, 0));
    idunn_box_set_align(actions, IDUNN_CENTER, IDUNN_END);
    idunn_add(actions, idunn_button("Details", IDUNN_OUTLINE, clicked, "Details"));
    idunn_add(actions, idunn_button("Clean up", IDUNN_PRIMARY, clicked, "Clean up"));
    section(page, "Card", "A title, a description, a body and actions on the right.", card);

    idunn_widget_t *tabs = idunn_tabs(NULL, NULL);
    idunn_widget_t *account = idunn_column(4, 0);
    idunn_add(account, idunn_field("Name", idunn_text_field("Ada Lovelace"), NULL));
    idunn_add(account, idunn_field("User name", idunn_text_field("ada"), "Used to sign in."));
    idunn_tabs_add(tabs, "Account", account);
    idunn_widget_t *password = idunn_column(4, 0);
    idunn_add(password, idunn_field("Current password", idunn_text_field(NULL), NULL));
    idunn_add(password, idunn_field("New password", idunn_text_field(NULL), NULL));
    idunn_tabs_add(tabs, "Password", password);
    idunn_widget_t *notes = idunn_column(2, 0);
    idunn_box_set_align(notes, IDUNN_START, IDUNN_START);
    idunn_add(notes, idunn_switch("Sounds", true, NULL, NULL));
    idunn_add(notes, idunn_switch("Show on the lock screen", false, NULL, NULL));
    idunn_tabs_add(tabs, "Notifications", notes);
    section(page, "Tabs", "One Tab stop; the arrows change the tab.", tabs);

    idunn_widget_t *badges = idunn_row(2, 0);
    idunn_box_set_align(badges, IDUNN_CENTER, IDUNN_START);
    idunn_add(badges, idunn_badge("Default", IDUNN_BADGE_DEFAULT));
    idunn_add(badges, idunn_badge("Secondary", IDUNN_BADGE_SECONDARY));
    idunn_add(badges, idunn_badge("Outline", IDUNN_BADGE_OUTLINE));
    idunn_add(badges, idunn_badge("Error", IDUNN_BADGE_DESTRUCTIVE));
    section(page, "Badges", "A word or a number in a small pill.", badges);

    idunn_widget_t *empty_card = idunn_card(0);
    idunn_widget_t *empty = idunn_add(empty_card,
                                      idunn_empty_state("inbox", "No messages",
                                                        "New mail shows up here."));
    idunn_add(empty, idunn_button("Check again", IDUNN_OUTLINE, clicked, "Check again"));
    section(page, "Empty state", "What a list shows when it has nothing in it.", empty_card);

    G.status = idunn_add(page, muted("Click a button.", IDUNN_TEXT_BODY));
    return page;
}

static void say(idunn_widget_t *w, void *data)
{
    (void)w;
    clicked(NULL, data);
}

static void open_file_menu(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_menu_popup(G.file_menu, button);
}

static void open_more_menu(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_menu_popup(G.more_menu, button);
}

static void make_menus(void)
{
    if (G.file_menu)
        return;
    G.file_menu = idunn_menu_new();
    idunn_menu_add(G.file_menu, "file-plus", "New file", "Ctrl+N", say, "New file");
    idunn_menu_add(G.file_menu, "folder", "Open...", "Ctrl+O", say, "Open");
    idunn_menu_t *recent = idunn_menu_new();
    idunn_menu_add(recent, "file-text", "notes.txt", NULL, say, "notes.txt");
    idunn_menu_add(recent, "file-text", "todo.txt", NULL, say, "todo.txt");
    idunn_menu_add(recent, "image", "wallpaper.png", NULL, say, "wallpaper.png");
    idunn_menu_add_sub(G.file_menu, "list", "Open recent", recent);
    idunn_menu_add_separator(G.file_menu);
    idunn_menu_add_check(G.file_menu, "Word wrap", true, say, "Word wrap");
    idunn_menu_add_check(G.file_menu, "Show hidden files", false, say, "Show hidden files");
    idunn_menu_add_separator(G.file_menu);
    int print = idunn_menu_add(G.file_menu, NULL, "Print", "Ctrl+P", say, "Print");
    idunn_menu_set_disabled(G.file_menu, print, true);
    idunn_menu_add(G.file_menu, "x", "Close", "Ctrl+W", say, "Close");

    G.more_menu = idunn_menu_new();
    idunn_menu_add(G.more_menu, "pencil", "Rename", "F2", say, "Rename");
    idunn_menu_add(G.more_menu, "copy", "Copy", "Ctrl+C", say, "Copy");
    idunn_menu_add(G.more_menu, "share", "Share", NULL, say, "Share");
    idunn_menu_add_separator(G.more_menu);
    idunn_menu_add(G.more_menu, "trash", "Move to trash", "Delete", say, "Move to trash");
}

static void open_popover(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_widget_t *content = idunn_column(3, 0);
    idunn_widget_t *head = idunn_add(content, idunn_column(1, 0));
    idunn_add(head, idunn_label("Dimensions", IDUNN_TEXT_LABEL));
    idunn_add(head, muted("Set the size of the picture.", IDUNN_TEXT_SMALL));
    idunn_add(content, idunn_field("Width", idunn_text_field("1440 px"), NULL));
    idunn_add(content, idunn_field("Height", idunn_text_field("900 px"), NULL));
    idunn_popover(button, content);
}

static idunn_widget_t *menus_page(void)
{
    make_menus();
    idunn_widget_t *page = idunn_column(6, 0);

    idunn_widget_t *menus = idunn_row(2, 0);
    idunn_box_set_align(menus, IDUNN_CENTER, IDUNN_START);
    idunn_widget_t *file = idunn_add(menus, idunn_button("File", IDUNN_OUTLINE, open_file_menu, NULL));
    idunn_button_set_icon(file, "chevron-down");
    idunn_widget_t *more = idunn_add(menus, idunn_icon_button("ellipsis", IDUNN_GHOST,
                                                              open_more_menu, NULL));
    idunn_set_tooltip(more, "More");
    section(page, "Dropdown menu", "Icons, shortcuts, checks, a sub-menu and a disabled item.",
            menus);

    static const char *const themes[] = {"Light", "Dark", "Like the system"};
    static const char *const accents[] = {"Rose", "Blue", "Green", "Orange", "Violet", "Neutral"};
    idunn_widget_t *selects = idunn_row(3, 0);
    idunn_box_set_align(selects, IDUNN_START, IDUNN_START);
    idunn_add(selects, idunn_field("Theme", idunn_select(themes, 3, 1, NULL, NULL), NULL));
    idunn_widget_t *accent = idunn_select(accents, 6, -1, NULL, NULL);
    idunn_select_set_placeholder(accent, "Pick a colour");
    idunn_add(selects, idunn_field("Accent", accent, NULL));
    section(page, "Select", "A value from a list; the arrows open it too.", selects);

    idunn_widget_t *tips = idunn_row(1, 0);
    idunn_box_set_align(tips, IDUNN_CENTER, IDUNN_START);
    static const char *const icons[][2] = {
        {"search", "Search"}, {"star", "Add to favourites"}, {"copy", "Copy"}, {"trash", "Delete"}};
    for (int i = 0; i < 4; i++)
        idunn_set_tooltip(idunn_add(tips, idunn_icon_button(icons[i][0], IDUNN_GHOST, NULL, NULL)),
                          icons[i][1]);
    section(page, "Tooltip", "Keep the pointer on a button for a moment.", tips);

    idunn_widget_t *pop = idunn_row(0, 0);
    idunn_add(pop, idunn_button("Dimensions", IDUNN_OUTLINE, open_popover, NULL));
    section(page, "Popover", "A card with a small form; Escape or a press outside closes it.",
            pop);

    G.status = idunn_add(page, muted("Choose something from a menu.", IDUNN_TEXT_BODY));
    return page;
}

static void ask_delete(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_widget_t *dialog =
        idunn_dialog("Delete notes.txt?", "It goes to the trash. You can get it back from there.");
    idunn_dialog_add_button(dialog, "Cancel", IDUNN_OUTLINE, NULL, NULL);
    idunn_dialog_add_button(dialog, "Delete", IDUNN_DESTRUCTIVE, say, "Delete");
    idunn_dialog_open(button, dialog);
}

static void ask_rename(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_widget_t *dialog = idunn_dialog("Rename", "Give the file a new name.");
    idunn_add(idunn_dialog_content(dialog),
              idunn_field("Name", idunn_text_field("notes.txt"), "Letters, digits, - and ."));
    idunn_dialog_add_button(dialog, "Cancel", IDUNN_OUTLINE, NULL, NULL);
    idunn_dialog_add_button(dialog, "Rename", IDUNN_PRIMARY, say, "Rename");
    idunn_dialog_open(button, dialog);
}

static idunn_widget_t *dialogs_page(void)
{
    idunn_widget_t *page = idunn_column(6, 0);
    idunn_widget_t *ask = idunn_row(2, 0);
    idunn_box_set_align(ask, IDUNN_CENTER, IDUNN_START);
    idunn_add(ask, idunn_button("Delete file", IDUNN_DESTRUCTIVE, ask_delete, NULL));
    idunn_add(ask, idunn_button("Rename file", IDUNN_OUTLINE, ask_rename, NULL));
    section(page, "Dialog",
            "Over the window, which gets no input until it closes. Escape is Cancel.", ask);
    G.status = idunn_add(page, muted("Open a dialog.", IDUNN_TEXT_BODY));
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

/** a page in a scroll view, 24 by 32 pixels inside */
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
        idunn_button_set_variant(G.nav[i], i == index ? IDUNN_OUTLINE : IDUNN_GHOST);
    idunn_label_set_text(G.title, pages[index].name);
    idunn_label_set_text(G.description, pages[index].description);
    G.status = NULL;
    idunn_widget_free(G.page);
    G.page = idunn_grow(idunn_add(G.main, page_view(index)), 1);
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
    G.page = idunn_grow(idunn_add(G.main, page_view(0)), 1);

    idunn_window_set_root(win, root);
    return idunn_app_run(G.app);
}

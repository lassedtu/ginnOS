#pragma once

/**
 * @file idunn.h
 * @brief idunn: the widget toolkit of ginnOS.
 *
 * an app makes a tree of widgets (rows and columns of labels, buttons and
 * so on), puts it in a window and runs the app loop. widgets keep their
 * state between frames; the toolkit lays them out, draws only what
 * changed, in step with the screen, and sends the pointer and the keys to
 * them. the look comes from the theme (idunn_theme.h).
 *
 *   idunn_app_t *app = idunn_app_new("Files");
 *   idunn_window_t *win = idunn_window_new(app, "Files", 720, 480);
 *   idunn_widget_t *col = idunn_column(4, 6);
 *   idunn_add(col, idunn_label("Rename file", IDUNN_TEXT_HEADING));
 *   idunn_add(col, idunn_button("Rename", IDUNN_PRIMARY, on_rename, NULL));
 *   idunn_window_set_root(win, col);
 *   return idunn_app_run(app);
 *
 * sizes are in design pixels (the theme's scale makes them larger);
 * gaps and paddings are in space units of 4 design pixels.
 */

#include "idunn_theme.h"

#include <gfx.h>
#include <heimdall_proto.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct heim_conn heim_conn_t; // as in heimdall.h

typedef struct idunn_app idunn_app_t;
typedef struct idunn_context idunn_context_t;
typedef struct idunn_window idunn_window_t;
typedef struct idunn_widget idunn_widget_t;

typedef void (*idunn_callback_t)(idunn_widget_t *widget, void *data);

// pointer shapes (the same values as HEIM_CURSOR_)
#define IDUNN_CURSOR_ARROW 0
#define IDUNN_CURSOR_TEXT  1
#define IDUNN_CURSOR_HAND  2

// modifier keys (the same values as HEIM_MOD_)
#define IDUNN_MOD_SHIFT 0x01
#define IDUNN_MOD_CTRL  0x02
#define IDUNN_MOD_ALT   0x04

// keys idunn uses (Linux key codes, as in sys/input.h)
#define IDUNN_KEY_ESCAPE    1
#define IDUNN_KEY_BACKSPACE 14
#define IDUNN_KEY_TAB       15
#define IDUNN_KEY_ENTER     28
#define IDUNN_KEY_A         30
#define IDUNN_KEY_X         45
#define IDUNN_KEY_C         46
#define IDUNN_KEY_V         47
#define IDUNN_KEY_SPACE     57
#define IDUNN_KEY_HOME      102
#define IDUNN_KEY_UP        103
#define IDUNN_KEY_PAGEUP    104
#define IDUNN_KEY_LEFT      105
#define IDUNN_KEY_RIGHT     106
#define IDUNN_KEY_END       107
#define IDUNN_KEY_DOWN      108
#define IDUNN_KEY_PAGEDOWN  109
#define IDUNN_KEY_DELETE    111

/**
 * connect to the window server and read the theme.
 * @param name the program's id, the name of its .app file
 *        ("idunn-gallery"): the shell shows its icon with its windows.
 * @return the app, or NULL (no server, or no fonts).
 */
idunn_app_t *idunn_app_new(const char *name);

/**
 * the connection to the window server, for calls idunn does not wrap
 * (the shell's heim_shell_start, for example).
 */
heim_conn_t *idunn_app_conn(const idunn_app_t *app);

/**
 * get the events that are about no window of the app (the shell's
 * HEIM_EV_WINDOW, HEIM_EV_WINDOW_APP, HEIM_EV_WINDOW_GONE), and
 * HEIM_EV_SETTINGS (after idunn applied a new theme).
 */
void idunn_app_on_event(idunn_app_t *app,
                        void (*on_event)(idunn_app_t *app, const heim_msg_t *msg, void *data),
                        void *data);

/**
 * call every(data) each period ms while the app runs (a clock). one per
 * app; a new call replaces it, period 0 stops it.
 */
void idunn_app_every(idunn_app_t *app, uint32_t period, void (*every)(void *data), void *data);

#define IDUNN_WATCH_MAX 4

/**
 * call ready(fd, data) when fd can be read (or has hung up) while the app
 * runs: a pty, a pipe. at most IDUNN_WATCH_MAX at a time; ready NULL
 * stops watching fd.
 */
void idunn_app_watch(idunn_app_t *app, int fd, void (*ready)(int fd, void *data), void *data);

/**
 * ask for the clipboard's text for the program itself (not a widget):
 * got gets it when it comes ("" when the clipboard is empty).
 */
void idunn_app_paste(idunn_app_t *app, void (*got)(const char *text, size_t length, void *data),
                     void *data);

/**
 * run until the last window is closed or idunn_app_quit.
 * @return the exit status for main.
 */
int idunn_app_run(idunn_app_t *app);
void idunn_app_quit(idunn_app_t *app);

/**
 * use another theme in all windows of the app (they are drawn again).
 */
void idunn_app_set_theme(idunn_app_t *app, const idunn_theme_t *theme);
/**
 * change a theme setting (mode, accent, radius, font_size, scale, motion)
 * in the user's theme file. every app and the window decorations change.
 * @return false if the file cannot be written; this app changes then.
 */
bool idunn_app_save_theme(idunn_app_t *app, const char *key, const char *value);
const idunn_theme_t *idunn_app_theme(const idunn_app_t *app);

/**
 * a window with a title bar; it shows when the app runs.
 * @param width the size of its content, in design pixels.
 */
idunn_window_t *idunn_window_new(idunn_app_t *app, const char *title, int32_t width,
                                 int32_t height);

/**
 * a panel of the desktop shell: no title bar, at the bottom of the screen,
 * as wide as the screen, above the windows (HEIM_WINDOW_PANEL).
 * @param height in design pixels.
 */
idunn_window_t *idunn_panel_new(idunn_app_t *app, int32_t height);

/**
 * the widget that fills the window (a row or a column, usually). the
 * window owns the tree from now on.
 */
void idunn_window_set_root(idunn_window_t *win, idunn_widget_t *root);

/**
 * keys that no widget used (the focused one and its parents): the
 * window's own shortcuts. on_key returns true when it used the key.
 * code is a Linux key code, mods IDUNN_MOD_, text what it types or "".
 */
typedef bool (*idunn_key_hook_t)(idunn_window_t *win, uint16_t code, uint32_t mods,
                                 const char *text, void *data);
void idunn_window_on_key(idunn_window_t *win, idunn_key_hook_t on_key, void *data);

/**
 * close and free the window (and its widgets).
 */
void idunn_window_close(idunn_window_t *win);

/**
 * a new title in the window's title bar (and the shell's window list).
 */
void idunn_window_set_title(idunn_window_t *win, const char *title);

// the tree and the layout

typedef enum
{
    IDUNN_START,
    IDUNN_CENTER,
    IDUNN_END,
    IDUNN_STRETCH,       // across a box: the full size (the default)
    IDUNN_SPACE_BETWEEN, // along a box: the free space between the children
} idunn_align_t;

/**
 * a box that puts its children in a row (left to right) or a column (top
 * to bottom), like a CSS flexbox.
 * @param gap the space between children, in units (4 design pixels).
 * @param padding the space inside the box, in units.
 */
idunn_widget_t *idunn_row(int gap, int padding);
idunn_widget_t *idunn_column(int gap, int padding);

/**
 * how a box places its children: align across it (default: stretch) and
 * justify along it (default: start).
 */
void idunn_box_set_align(idunn_widget_t *box, idunn_align_t align, idunn_align_t justify);

/**
 * other paddings for the sides of a box, in units: across and along.
 */
void idunn_box_set_padding(idunn_widget_t *box, int vertical, int horizontal);

/**
 * a background for a box, a colour token (by default it has none).
 */
void idunn_box_set_background(idunn_widget_t *box, idunn_color_t color);
/**
 * a row that is too short for its children shows the last ones: the
 * first go out of view on the left, whole (a path bar keeps the folder
 * that is open in view).
 */
void idunn_box_set_keep_end(idunn_widget_t *box, bool keep_end);

/**
 * a card: a column on the card colour with a border, the large radius and
 * a small shadow, 24 pixels inside.
 * @param gap the space between its children, in units.
 */
idunn_widget_t *idunn_card(int gap);

/**
 * a title and a muted description (or NULL) at the top of a card.
 */
void idunn_card_header(idunn_widget_t *card, const char *title, const char *description);

/**
 * a scroll view: the child can be taller than the view. the view takes
 * the height it is given (let it grow, or give it a size), as a CSS box
 * with min-height 0. the wheel, the keys its children do not use and a
 * thin scroll bar scroll it; Tab scrolls the focus into view.
 */
idunn_widget_t *idunn_scroll_view(idunn_widget_t *child);
int32_t idunn_scroll_view_offset(const idunn_widget_t *view);
void idunn_scroll_view_scroll_to(idunn_widget_t *view, int32_t offset);

/**
 * empty space that grows (put it between children to push them apart).
 */
idunn_widget_t *idunn_spacer(void);

/**
 * add child at the end of parent.
 * @return child.
 */
idunn_widget_t *idunn_add(idunn_widget_t *parent, idunn_widget_t *child);

/**
 * take a widget out of its parent and free it and its children.
 */
void idunn_widget_free(idunn_widget_t *w);

/**
 * let a widget take a share of the free space along its box.
 * @return w.
 */
idunn_widget_t *idunn_grow(idunn_widget_t *w, int weight);

/**
 * a size a widget must have, in design pixels (0: its own size).
 */
void idunn_set_size(idunn_widget_t *w, int32_t width, int32_t height);

/**
 * a hidden widget is not drawn, takes no room in its box and gets no input.
 */
void idunn_set_hidden(idunn_widget_t *w, bool hidden);

/**
 * a disabled widget is drawn at half opacity and gets no input.
 */
void idunn_set_disabled(idunn_widget_t *w, bool disabled);
/**
 * on_menu on a right click on the widget or in it, when no widget in it
 * used the click (a list view uses it on its items: idunn_list_view_on_menu).
 * open a menu with idunn_menu_popup_at_pointer.
 */
void idunn_on_menu(idunn_widget_t *w, idunn_callback_t on_menu, void *data);

/**
 * a control with a wrong value: its border and ring are red (a field
 * sets this on its control with idunn_field_set_error).
 */
void idunn_set_error(idunn_widget_t *w, bool error);

// the widgets

/**
 * text in a style; it ends with "..." when it is too wide, or wraps.
 */
idunn_widget_t *idunn_label(const char *text, idunn_text_t style);
void idunn_label_set_text(idunn_widget_t *label, const char *text);
void idunn_label_set_color(idunn_widget_t *label, idunn_color_t color);
void idunn_label_set_wrap(idunn_widget_t *label, bool wrap);

typedef enum
{
    IDUNN_PRIMARY,     // the main action: the accent
    IDUNN_SECONDARY,   // a quiet filled button
    IDUNN_OUTLINE,     // a border and a light fill
    IDUNN_GHOST,       // only on hover (toolbars, icon buttons)
    IDUNN_DESTRUCTIVE, // delete: soft red
    IDUNN_LINK,        // text, underlined on hover
} idunn_variant_t;

typedef enum
{
    IDUNN_SIZE_DEFAULT, // 32 pixels high
    IDUNN_SIZE_SMALL,   // 28
    IDUNN_SIZE_LARGE,   // 36
} idunn_size_t;

/**
 * a button; on_click gets the button and data.
 * @param text the label, or NULL for an icon button.
 */
idunn_widget_t *idunn_button(const char *text, idunn_variant_t variant, idunn_callback_t on_click,
                             void *data);

/**
 * a square button with only an icon (a Lucide name).
 */
idunn_widget_t *idunn_icon_button(const char *icon, idunn_variant_t variant,
                                  idunn_callback_t on_click, void *data);

/**
 * an icon (a Lucide name) before the text.
 */
void idunn_button_set_icon(idunn_widget_t *button, const char *icon);
void idunn_button_set_size(idunn_widget_t *button, idunn_size_t size);
void idunn_button_set_text(idunn_widget_t *button, const char *text);
void idunn_button_set_variant(idunn_widget_t *button, idunn_variant_t variant);

/**
 * where the icon and text go: IDUNN_CENTER (the default) or IDUNN_START
 * (side bar items, menu-like lists).
 */
void idunn_button_set_align(idunn_widget_t *button, idunn_align_t align);

/**
 * a 1 pixel line: across a column, or down a row.
 */
idunn_widget_t *idunn_separator(void);

/**
 * a one-line text field, 32 pixels high.
 * @param placeholder the muted text it shows when it is empty, or NULL.
 */
idunn_widget_t *idunn_text_field(const char *placeholder);

/** the text (UTF-8); valid until the text changes */
const char *idunn_text_field_text(const idunn_widget_t *field);
void idunn_text_field_set_text(idunn_widget_t *field, const char *text);
/**
 * select bytes start to end (the caret at end); typing replaces them.
 * (size_t)-1 is the end of the text.
 */
void idunn_text_field_select(idunn_widget_t *field, size_t start, size_t end);

/** an icon (a Lucide name) at the start, as in a search field */
void idunn_text_field_set_icon(idunn_widget_t *field, const char *icon);

/** on_change after every change of the text; on_submit on Enter */
void idunn_text_field_on_change(idunn_widget_t *field, idunn_callback_t on_change, void *data);
void idunn_text_field_on_submit(idunn_widget_t *field, idunn_callback_t on_submit, void *data);

/**
 * a text area: lines that wrap, Enter for a new line; it scrolls.
 * @param rows the lines it shows (its height), 0 for 4.
 */
idunn_widget_t *idunn_text_area(const char *placeholder, int rows);
const char *idunn_text_area_text(const idunn_widget_t *area);
void idunn_text_area_set_text(idunn_widget_t *area, const char *text);
void idunn_text_area_on_change(idunn_widget_t *area, idunn_callback_t on_change, void *data);

typedef enum
{
    IDUNN_UNCHECKED,
    IDUNN_CHECKED,
    IDUNN_MIXED, // some of the things it stands for (a dash)
} idunn_check_t;

/**
 * a check box with a label to its right (a click on the label clicks it).
 * on_change comes after a click or Space.
 */
idunn_widget_t *idunn_check_box(const char *label, bool checked, idunn_callback_t on_change,
                                void *data);
idunn_check_t idunn_check_box_state(const idunn_widget_t *box);
void idunn_check_box_set_state(idunn_widget_t *box, idunn_check_t state);

/**
 * a group of choices with round buttons, one chosen; one Tab stop, the
 * arrows move the choice. in a column (or a row: idunn_radio_group_set_row).
 * @param chosen the index of the chosen one, or -1 for none.
 */
idunn_widget_t *idunn_radio_group(const char *const *labels, int count, int chosen,
                                  idunn_callback_t on_change, void *data);
int idunn_radio_group_chosen(const idunn_widget_t *group);
void idunn_radio_group_choose(idunn_widget_t *group, int chosen);
void idunn_radio_group_set_row(idunn_widget_t *group, bool row);

/**
 * a switch with a label to its right; it slides when it changes.
 */
idunn_widget_t *idunn_switch(const char *label, bool on, idunn_callback_t on_change, void *data);
bool idunn_switch_on(const idunn_widget_t *sw);
void idunn_switch_set_on(idunn_widget_t *sw, bool on);

/**
 * a slider from min to max in steps (the arrows move it by one step).
 */
idunn_widget_t *idunn_slider(int32_t min, int32_t max, int32_t step, int32_t value,
                             idunn_callback_t on_change, void *data);
int32_t idunn_slider_value(const idunn_widget_t *slider);
void idunn_slider_set_value(idunn_widget_t *slider, int32_t value);

typedef enum
{
    IDUNN_BADGE_DEFAULT, // the accent
    IDUNN_BADGE_SECONDARY,
    IDUNN_BADGE_OUTLINE,
    IDUNN_BADGE_DESTRUCTIVE,
} idunn_badge_t;

/** a badge: a short word in a small pill */
idunn_widget_t *idunn_badge(const char *text, idunn_badge_t variant);

/** a Lucide icon on its own, size points, in a colour token */
idunn_widget_t *idunn_icon_view(const char *name, int32_t size, idunn_color_t color);

/**
 * an empty state: an icon on a muted square, a title and a muted line (or
 * NULL), in the middle of its space. add buttons to it for actions.
 */
idunn_widget_t *idunn_empty_state(const char *icon, const char *title, const char *text);

#define IDUNN_PROGRESS_MAX 1000
/** a progress bar across its box; value 0 to IDUNN_PROGRESS_MAX */
idunn_widget_t *idunn_progress(void);
void idunn_progress_set(idunn_widget_t *progress, int32_t value);

/**
 * tabs: a strip of names and the page of the active one below it. the
 * strip is one Tab stop; the arrows change the tab.
 */
idunn_widget_t *idunn_tabs(idunn_callback_t on_change, void *data);
/** add a tab with its page (the tabs own it); the first one is active */
idunn_widget_t *idunn_tabs_add(idunn_widget_t *tabs, const char *name, idunn_widget_t *page);
int idunn_tabs_active(const idunn_widget_t *tabs);
void idunn_tabs_set_active(idunn_widget_t *tabs, int active);

/**
 * a list view: rows with a Lucide icon (or NULL), text and a muted second
 * text on the right (or NULL). put it in a scroll view when it is long.
 */
idunn_widget_t *idunn_list_view(void);
/**
 * a grid view: the same, as tiles of a 48 point Tela icon (a file or
 * folder icon name) with the name below; it wraps to the width.
 */
idunn_widget_t *idunn_grid_view(void);
/** @return the index of the new item, or -1 */
int idunn_list_view_add(idunn_widget_t *list, const char *icon, const char *text,
                        const char *detail);
void idunn_list_view_clear(idunn_widget_t *list);
int idunn_list_view_count(const idunn_widget_t *list);
const char *idunn_list_view_text(const idunn_widget_t *list, int item);
/** many items can be selected (Ctrl, Shift, Ctrl+A) */
void idunn_list_view_set_multiple(idunn_widget_t *list, bool multiple);
/** rows with a Tela icon (a file or folder icon name) in colour, not a Lucide one */
void idunn_list_view_set_pictures(idunn_widget_t *list, bool pictures);
/** a border, the large radius and the card colour around the items */
void idunn_list_view_set_framed(idunn_widget_t *list, bool framed);
bool idunn_list_view_selected(const idunn_widget_t *list, int item);
void idunn_list_view_select(idunn_widget_t *list, int item, bool selected);
/** the item with the keyboard, or -1 */
int idunn_list_view_current(const idunn_widget_t *list);
/** on_select after the selection changes; on_activate on a double click or Enter */
void idunn_list_view_on_select(idunn_widget_t *list, idunn_callback_t on_select, void *data);
void idunn_list_view_on_activate(idunn_widget_t *list, idunn_callback_t on_activate, void *data);
/**
 * on_menu on a right click: on an item that is not selected, it selects
 * only that item first; on no item, nothing stays selected. open a menu
 * with idunn_menu_popup_at_pointer.
 */
void idunn_list_view_on_menu(idunn_widget_t *list, idunn_callback_t on_menu, void *data);
/** the item at a point of the window, or -1 */
int idunn_list_view_item_at(const idunn_widget_t *list, int32_t x, int32_t y);

// things that float: menus, selects, tooltips, popovers and dialogs

/** a menu: items the app adds, shown in a popup by idunn_menu_popup */
typedef struct idunn_menu idunn_menu_t;

idunn_menu_t *idunn_menu_new(void);
/** free a menu (not while it is open) and its sub-menus */
void idunn_menu_free(idunn_menu_t *menu);
/**
 * an item: a Lucide icon (or NULL), text and a shortcut to show (or NULL).
 * on_choose gets the widget the menu was opened for.
 * @return the index of the item.
 */
int idunn_menu_add(idunn_menu_t *menu, const char *icon, const char *text, const char *shortcut,
                   idunn_callback_t on_choose, void *data);
/** an item with a check that a choice turns on and off */
int idunn_menu_add_check(idunn_menu_t *menu, const char *text, bool checked,
                         idunn_callback_t on_choose, void *data);
void idunn_menu_add_separator(idunn_menu_t *menu);
/** an item that opens a sub-menu (the menu owns it) */
int idunn_menu_add_sub(idunn_menu_t *menu, const char *icon, const char *text, idunn_menu_t *sub);
void idunn_menu_set_disabled(idunn_menu_t *menu, int item, bool disabled);
bool idunn_menu_checked(const idunn_menu_t *menu, int item);
void idunn_menu_set_checked(idunn_menu_t *menu, int item, bool checked);
/** the least width of the menu, in design pixels (160 by default) */
void idunn_menu_set_min_width(idunn_menu_t *menu, int32_t width);
/** open the menu under a widget (a button's on_click does it) */
void idunn_menu_popup(idunn_menu_t *menu, idunn_widget_t *owner);
/** open the menu where the last right click was (a context menu) */
void idunn_menu_popup_at_pointer(idunn_menu_t *menu, idunn_widget_t *owner);

/**
 * a select: a field with the chosen value; it opens a menu of the choices.
 * @param chosen the index of the chosen one, or -1 (the placeholder shows).
 */
idunn_widget_t *idunn_select(const char *const *choices, int count, int chosen,
                             idunn_callback_t on_change, void *data);
int idunn_select_chosen(const idunn_widget_t *select);
void idunn_select_choose(idunn_widget_t *select, int chosen);
void idunn_select_set_placeholder(idunn_widget_t *select, const char *text);

/** a tooltip: shown over the widget after 600 ms under the pointer (NULL: none) */
void idunn_set_tooltip(idunn_widget_t *w, const char *text);

/**
 * a popover: content in a card under owner; a press outside it or Escape
 * closes it. the popover owns content.
 */
void idunn_popover(idunn_widget_t *owner, idunn_widget_t *content);
/** close the popover a widget is in */
void idunn_popover_close(idunn_widget_t *inside);

/**
 * a dialog: a card with a title and a description (or NULL), in the middle
 * of the window over the overlay colour. add content to
 * idunn_dialog_content, then the buttons; a button closes the dialog and
 * then calls its on_click (with the dialog). Escape closes it too.
 */
idunn_widget_t *idunn_dialog(const char *title, const char *description);
idunn_widget_t *idunn_dialog_content(idunn_widget_t *dialog);
/** a button at the bottom right: Cancel as outline, the action primary or destructive */
idunn_widget_t *idunn_dialog_add_button(idunn_widget_t *dialog, const char *text,
                                        idunn_variant_t variant, idunn_callback_t on_click,
                                        void *data);
/** show a dialog over the window of owner; the window owns it then */
void idunn_dialog_open(idunn_widget_t *owner, idunn_widget_t *dialog);
void idunn_dialog_close(idunn_widget_t *dialog);

/**
 * a form row: a label above the control and help text below it (or NULL).
 * the field owns the control.
 */
idunn_widget_t *idunn_field(const char *label, idunn_widget_t *control, const char *help);

/**
 * show an error: the message in red under the control, and the control's
 * border and ring red. NULL takes it away (the help text comes back).
 */
void idunn_field_set_error(idunn_widget_t *field, const char *message);

// without the window server: for tests and pictures

/**
 * fonts and icons for a theme.
 * @param font_dir the directory with inter/ and jetbrains-mono/.
 * @param icon_pack the Lucide pack, or NULL (no icons).
 * @return the context, or NULL (no fonts).
 */
idunn_context_t *idunn_context_new(const idunn_theme_t *theme, const char *font_dir,
                                   const char *icon_pack);
void idunn_context_free(idunn_context_t *ctx);
void idunn_context_set_theme(idunn_context_t *ctx, const idunn_theme_t *theme);

/**
 * a window that draws into its own buffer, of that many pixels.
 */
idunn_window_t *idunn_window_offscreen(idunn_context_t *ctx, int32_t width, int32_t height);

/**
 * a new size for the window's buffer (the window lays out again).
 */
void idunn_window_resize(idunn_window_t *win, int32_t width, int32_t height);

gfx_buffer_t *idunn_window_buffer(const idunn_window_t *win);

/**
 * lay out and draw what changed.
 * @return the part of the buffer that changed (empty: none).
 */
gfx_rect_t idunn_window_draw(idunn_window_t *win);

/**
 * the whole window must be drawn again (after a theme change).
 */
void idunn_window_invalidate(idunn_window_t *win);

typedef enum
{
    IDUNN_POINTER_MOTION,
    IDUNN_POINTER_PRESS,   // the left button
    IDUNN_POINTER_RELEASE,
    IDUNN_POINTER_LEAVE,
    IDUNN_POINTER_MENU,    // the right button went down: a context menu
} idunn_pointer_t;

/**
 * the pointer moved, or the left button went down or up, at (x, y),
 * with these modifier keys held (Shift+click extends a selection).
 */
void idunn_window_pointer(idunn_window_t *win, idunn_pointer_t kind, int32_t x, int32_t y,
                          uint32_t mods);

/**
 * the wheel turned over (x, y): steps, up is positive.
 */
void idunn_window_scroll(idunn_window_t *win, int32_t x, int32_t y, int32_t steps);

/**
 * the clipboard text a widget asked for came (from the window server).
 */
void idunn_window_paste(idunn_window_t *win, const char *text, size_t length);

/**
 * the next time (ms) the window must be ticked for a timer, or 0 for none.
 */
uint32_t idunn_window_next_wake(const idunn_window_t *win);

/**
 * a key press (or repeat).
 * @param code a Linux key code (KEY_ in sys/input.h).
 * @param text the characters it types (UTF-8), or "".
 */
void idunn_window_key(idunn_window_t *win, uint16_t code, uint32_t mods, const char *text);

/**
 * the time now in ms, for transitions.
 * @return true while a transition runs (draw again in a frame).
 */
bool idunn_window_tick(idunn_window_t *win, uint32_t now);

/**
 * the pointer shape for where the pointer is (IDUNN_CURSOR_).
 */
uint32_t idunn_window_cursor(const idunn_window_t *win);

/**
 * the widget with the keyboard focus, or NULL.
 */
idunn_widget_t *idunn_window_focus(const idunn_window_t *win);

typedef enum
{
    IDUNN_INPUT_KEY,     // code, mods, text (a press or a repeat)
    IDUNN_INPUT_PRESS,   // the left button went down: x, y, mods, clicks (1 to 3)
    IDUNN_INPUT_DRAG,    // the pointer moved with the button down: x, y
    IDUNN_INPUT_RELEASE, // the button went up: x, y
    IDUNN_INPUT_MOTION,  // the pointer moved, no button down: x, y
    IDUNN_INPUT_SCROLL,  // the wheel: steps, up is positive
    IDUNN_INPUT_PASTE,   // the clipboard text it asked for: text, length
    IDUNN_INPUT_FOCUS,   // it got the keyboard
    IDUNN_INPUT_BLUR,    // it lost the keyboard
    IDUNN_INPUT_TIMER,   // the time it asked for (idunn_canvas_wake) came
} idunn_input_type_t;

/**
 * input for a canvas. x and y are from the canvas's top left corner.
 */
typedef struct
{
    idunn_input_type_t type;
    uint16_t code;    // KEY: the Linux key code
    uint32_t mods;    // KEY, PRESS: IDUNN_MOD_
    const char *text; // KEY: the characters it types (UTF-8), or ""; PASTE: the text
    size_t length;    // PASTE
    int32_t x;
    int32_t y;
    int clicks;
    int32_t steps;
} idunn_input_t;

/**
 * what a canvas does; each hook can be NULL.
 */
typedef struct
{
    /**
     * draw the canvas, which is at rect in the window. only the part in
     * buf->clip must be drawn (the rest is still right).
     */
    void (*draw)(idunn_widget_t *canvas, gfx_buffer_t *buf, gfx_rect_t rect, void *data);
    /** @return true if it used the input (else it goes to the parents) */
    bool (*input)(idunn_widget_t *canvas, const idunn_input_t *in, void *data);
    /** the canvas has a new size (also the first time it is laid out) */
    void (*resize)(idunn_widget_t *canvas, int32_t width, int32_t height, void *data);
    /**
     * before a frame is drawn, while the window still holds the last
     * frame: move pixels (idunn_canvas_move) and say what to draw again.
     */
    void (*frame)(idunn_widget_t *canvas, void *data);
} idunn_canvas_hooks_t;

/**
 * a widget the program draws itself: a terminal, a game. it takes the
 * keyboard (Tab too), the pointer and the wheel. it is as large as its
 * box lets it grow, or idunn_set_size.
 */
idunn_widget_t *idunn_canvas(const idunn_canvas_hooks_t *hooks, void *data);

/** draw a rect of the canvas again (canvas coordinates) */
void idunn_canvas_damage(idunn_widget_t *canvas, gfx_rect_t r);

/** a frame soon, so the frame hook runs (new data came) */
void idunn_canvas_update(idunn_widget_t *canvas);

/**
 * in the frame hook: move the pixels of area up (dy < 0) or down; the
 * strip that comes into view is damaged.
 */
void idunn_canvas_move(idunn_widget_t *canvas, gfx_rect_t area, int32_t dy);

/** put text on the clipboard; ask for its text (an IDUNN_INPUT_PASTE comes) */
void idunn_canvas_copy(idunn_widget_t *canvas, const char *text, size_t length);
void idunn_canvas_paste(idunn_widget_t *canvas);

/** an IDUNN_INPUT_TIMER in ms (one at a time; a new call moves it) */
void idunn_canvas_wake(idunn_widget_t *canvas, uint32_t ms);

/** the pointer shape over the canvas (IDUNN_CURSOR_) */
void idunn_canvas_set_cursor(idunn_widget_t *canvas, uint32_t shape);

/**
 * give the keyboard to a widget in a window (a canvas at the start).
 */
void idunn_set_focus(idunn_widget_t *w);

/**
 * where a widget is in its window, after layout.
 */
gfx_rect_t idunn_widget_rect(const idunn_widget_t *w);

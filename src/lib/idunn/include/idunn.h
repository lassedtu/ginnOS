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
#include <stdbool.h>
#include <stdint.h>

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
 * @return the app, or NULL (no server, or no fonts).
 */
idunn_app_t *idunn_app_new(const char *name);

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
const idunn_theme_t *idunn_app_theme(const idunn_app_t *app);

/**
 * a window with a title bar; it shows when the app runs.
 * @param width the size of its content, in design pixels.
 */
idunn_window_t *idunn_window_new(idunn_app_t *app, const char *title, int32_t width,
                                 int32_t height);

/**
 * the widget that fills the window (a row or a column, usually). the
 * window owns the tree from now on.
 */
void idunn_window_set_root(idunn_window_t *win, idunn_widget_t *root);

/**
 * close and free the window (and its widgets).
 */
void idunn_window_close(idunn_window_t *win);

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

/**
 * where a widget is in its window, after layout.
 */
gfx_rect_t idunn_widget_rect(const idunn_widget_t *w);

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
#define IDUNN_KEY_ESCAPE 1
#define IDUNN_KEY_TAB    15
#define IDUNN_KEY_ENTER  28
#define IDUNN_KEY_A      30
#define IDUNN_KEY_SPACE  57

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
 * a disabled widget is drawn at half opacity and gets no input.
 */
void idunn_set_disabled(idunn_widget_t *w, bool disabled);

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
 * the pointer moved, or the left button went down or up, at (x, y).
 */
void idunn_window_pointer(idunn_window_t *win, idunn_pointer_t kind, int32_t x, int32_t y);

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

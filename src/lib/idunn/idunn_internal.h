#pragma once

/**
 * @file idunn_internal.h
 * @brief idunn: what the widget classes share (not for apps).
 */

#include "idunn.h"

#include <bragi.h>
#include <icon.h>

// widget flags (what it can do)
#define IDUNN_FOCUSABLE   0x01u // Tab and a click give it the keyboard
#define IDUNN_INTERACTIVE 0x02u // it gets hover, presses and clicks
#define IDUNN_SHRINKS     0x04u // it can be narrower than it wants (text with "...")

// widget states
#define IDUNN_HOVER    0x01u
#define IDUNN_PRESSED  0x02u
#define IDUNN_FOCUSED  0x04u
#define IDUNN_DISABLED 0x08u

typedef enum
{
    IDUNN_EV_CLICK, // a press and a release on the widget, or Enter or Space
    IDUNN_EV_KEY,   // a key press for the focused widget (and then its parents)
} idunn_event_type_t;

typedef struct
{
    idunn_event_type_t type;
    uint16_t code;    // KEY: the Linux key code
    uint32_t mods;    // KEY: IDUNN_MOD_
    const char *text; // KEY: the characters it types (UTF-8), or ""
} idunn_event_t;

/**
 * a transition of one value (0 to 1) with the CSS ease-out curve.
 */
typedef struct
{
    float from;
    float to;
    uint32_t start;    // ms
    uint32_t duration; // ms; 0: no transition
} idunn_anim_t;

typedef struct
{
    const char *name;
    /** the size it wants, in pixels; max_width is the width there is (for text that wraps) */
    void (*measure)(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height);
    /** place the children in w->rect (boxes) */
    void (*layout)(idunn_widget_t *w);
    /** draw in w->rect; the children are drawn after it */
    void (*draw)(idunn_widget_t *w, gfx_buffer_t *buf);
    /** @return true if the widget used the event */
    bool (*event)(idunn_widget_t *w, const idunn_event_t *ev);
    /** free w->data */
    void (*free)(idunn_widget_t *w);
} idunn_class_t;

struct idunn_widget
{
    const idunn_class_t *cls;
    idunn_window_t *window; // NULL until the tree is in a window
    idunn_widget_t *parent;
    idunn_widget_t **children;
    int child_count;
    int child_capacity;
    gfx_rect_t rect; // in the window, after layout
    uint32_t flags;
    uint32_t state;
    uint32_t cursor;      // IDUNN_CURSOR_ over it
    int grow;             // a share of the free space in its box (0: none)
    int32_t width;        // a size it must have, in design pixels (0: its own)
    int32_t height;
    idunn_anim_t hover;   // 0 to 1: the hover look
    idunn_anim_t press;   // 0 to 1: the pressed look
    void *data;           // the class's own state
};

struct idunn_context
{
    idunn_theme_t theme;
    bragi_font_t *fonts[4]; // Inter 400, 500, 600 and JetBrains Mono
    bragi_face_t *faces[IDUNN_TEXT_COUNT];
    icon_set_t *icons;      // Lucide
    struct icon_entry *icon_cache;
};

struct idunn_window
{
    idunn_context_t *ctx;
    gfx_buffer_t *buffer;
    bool own_buffer;
    idunn_widget_t *root;
    idunn_widget_t *hover;   // the interactive widget under the pointer
    idunn_widget_t *pressed; // from a press until the release
    idunn_widget_t *focus;   // it gets the keys
    bool focus_visible;      // the focus ring shows (after a key, not a click)
    bool needs_layout;
    bool animating;          // a transition runs: draw the next frame too
    gfx_rect_t damage;       // what must be drawn again
    uint32_t now;            // ms, for transitions
    uint32_t cursor;         // the pointer shape now

    // for app.c
    idunn_app_t *app;
    void *heim;
    idunn_window_t *next;
};

// the context
bragi_face_t *idunn_face(idunn_context_t *ctx, idunn_text_t style);
const icon_t *idunn_icon(idunn_context_t *ctx, const char *name, int32_t size);

static inline idunn_context_t *idunn_ctx(const idunn_widget_t *w)
{
    return w->window->ctx;
}

static inline const idunn_theme_t *idunn_theme(const idunn_widget_t *w)
{
    return &w->window->ctx->theme;
}

// widgets
idunn_widget_t *idunn_widget_new(const idunn_class_t *cls, void *data, uint32_t flags);
void idunn_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height);
void idunn_place(idunn_widget_t *w, gfx_rect_t rect);
void idunn_dirty(idunn_widget_t *w);
void idunn_relayout(idunn_widget_t *w);
void idunn_set_window(idunn_widget_t *w, idunn_window_t *win);
bool idunn_is_column(const idunn_widget_t *w);

// windows (for app.c)
void idunn_window_use_buffer(idunn_window_t *win, gfx_buffer_t *buf);
void idunn_window_free_widgets(idunn_window_t *win);

/** the hover and press looks now (0 to 255), for drawing */
uint8_t idunn_hover_amount(idunn_widget_t *w);
uint8_t idunn_press_amount(idunn_widget_t *w);
/** true when the focus ring shows */
bool idunn_ring_visible(const idunn_widget_t *w);

void idunn_anim_to(idunn_anim_t *a, float to, uint32_t now, uint32_t duration);
float idunn_anim_value(const idunn_anim_t *a, uint32_t now);
bool idunn_anim_running(const idunn_anim_t *a, uint32_t now);

// drawing

/**
 * a line of text in a style with the top of its line box at y; it ends
 * with "..." if it is wider than max_width (0: any width).
 */
void idunn_draw_text(idunn_context_t *ctx, gfx_buffer_t *buf, idunn_text_t style, int32_t x,
                     int32_t y, int32_t max_width, const char *text, gfx_color_t color);

/** the 3 px focus ring around a round box */
void idunn_draw_ring(const idunn_widget_t *w, gfx_buffer_t *buf, gfx_rect_t box, int32_t radius);

/** how far drawing goes out of a widget's rect (rings and shadows) */
int32_t idunn_overdraw(const idunn_theme_t *theme);

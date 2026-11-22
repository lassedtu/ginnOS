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
#define IDUNN_TEXT_INPUT  0x08u // it takes typing: the ring shows after a click too
#define IDUNN_CLIPS       0x10u // its children are drawn and hit only inside its rect
#define IDUNN_TAKES_TAB   0x20u // Tab goes to it, not to the next widget (a terminal)
#define IDUNN_OWN_LOOK    0x40u // it draws its own hover, press and focus (a canvas)

// widget states
#define IDUNN_HOVER    0x01u
#define IDUNN_PRESSED  0x02u
#define IDUNN_FOCUSED  0x04u
#define IDUNN_DISABLED 0x08u
#define IDUNN_ERROR    0x10u // a field with a wrong value
#define IDUNN_HIDDEN   0x20u // not drawn, not laid out in a box, no input

#define IDUNN_DAMAGE_MAX 8 // damaged rects a window keeps apart

/** add a rect of the window that must be drawn again */
void idunn_window_add_damage(idunn_window_t *win, gfx_rect_t r);

/**
 * lay out and draw what changed, part by part.
 * @return how many parts were drawn (in parts).
 */
int idunn_window_draw_parts(idunn_window_t *win, gfx_rect_t parts[IDUNN_DAMAGE_MAX + 1]);

/**
 * move the pixels of a rect of the window up (dy < 0) or down, as when its
 * content scrolls: what stays in it is not drawn again, only the strip
 * that comes into view. w is the widget the rect belongs to (its clip).
 */
void idunn_window_move_pixels(idunn_widget_t *w, gfx_rect_t area, int32_t dy);

typedef enum
{
    IDUNN_EV_CLICK,   // a press and a release on the widget (or Enter or Space)
    IDUNN_EV_KEY,     // a key press for the focused widget (and then its parents)
    IDUNN_EV_PRESS,   // the left button went down on it: x, y, mods, clicks
    IDUNN_EV_DRAG,    // the pointer moved with the button down after a press on it: x, y
    IDUNN_EV_RELEASE, // the button went up after a press on it: x, y
    IDUNN_EV_SCROLL,  // the wheel over it (then its parents): steps, up is positive
    IDUNN_EV_FOCUS,   // it got the keyboard
    IDUNN_EV_BLUR,    // it lost the keyboard
    IDUNN_EV_TIMER,   // the time it asked for with idunn_wake came
    IDUNN_EV_PASTE,   // the clipboard text it asked for with idunn_paste: text, length
    IDUNN_EV_MOTION,  // the pointer moved over it (no button down): x, y
    IDUNN_EV_MENU,    // the right button went down on it (then its parents): x, y, mods
} idunn_event_type_t;

typedef struct
{
    idunn_event_type_t type;
    uint16_t code;    // KEY: the Linux key code
    uint32_t mods;    // KEY, PRESS: IDUNN_MOD_
    const char *text; // KEY: the characters it types (UTF-8), or ""; PASTE: the text
    size_t length;    // PASTE: the bytes of text
    int32_t x;        // PRESS, DRAG, RELEASE: the pointer in the window
    int32_t y;
    int clicks;       // PRESS: 1, 2 (a double click) or 3
    int32_t steps;    // SCROLL
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
    /** draw over the children (a scroll bar), in the same clip as them */
    void (*draw_over)(idunn_widget_t *w, gfx_buffer_t *buf);
    /** @return true if the widget used the event */
    bool (*event)(idunn_widget_t *w, const idunn_event_t *ev);
    /**
     * scroll so that r (a rect of a widget in w) can be seen.
     * @return where r is after the scroll.
     */
    gfx_rect_t (*reveal)(idunn_widget_t *w, gfx_rect_t r);
    /**
     * the part of w that looks different when the pointer comes or goes
     * (optional: all of w). a large widget that shows hover in a small
     * part (a scroll bar, a list row) is then not drawn again whole.
     */
    gfx_rect_t (*hover_area)(idunn_widget_t *w);
    /**
     * before each frame is drawn (optional): move things that glide (a
     * scroll view), when the pixels in the buffer can still be moved
     */
    void (*frame)(idunn_widget_t *w);
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
    char *tooltip;        // shown over it after a while under the pointer, or NULL
    void *data;           // the class's own state
};

struct idunn_context
{
    idunn_theme_t theme;
    bragi_font_t *fonts[4]; // Inter 400, 500, 600 and JetBrains Mono
    bragi_face_t *faces[IDUNN_TEXT_COUNT];
    icon_set_t *icons;      // Lucide
    icon_set_t *pictures;   // Tela (colour), opened when first needed
    bool pictures_tried;
    char icon_dir[200];     // where the packs are
    struct icon_entry *icon_cache;
    idunn_window_t *dead; // closed popups, to be freed
    idunn_widget_t **dead_widgets; // closed dialogs, to be freed
    int dead_count;
    int dead_capacity;
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
    gfx_rect_t damage;       // what must be drawn again: all of it
    gfx_rect_t damage_list[IDUNN_DAMAGE_MAX]; // and in parts (far apart ones stay apart)
    int damage_count;
    gfx_rect_t moved; // pixels moved in the buffer: not drawn again, but sent
    uint32_t now;            // ms, for transitions
    uint32_t cursor;         // the pointer shape now

    // a double click: the last press
    uint32_t press_time;
    int32_t press_x;
    int32_t press_y;
    int clicks;
    // the last right press, where a context menu opens
    int32_t menu_x;
    int32_t menu_y;

    // keys no widget used (idunn_window_on_key)
    idunn_key_hook_t on_key;
    void *on_key_data;

    // times widgets asked to be woken at (idunn_wake)
    struct
    {
        idunn_widget_t *widget;
        uint32_t at;
    } timers[8];

    // the clipboard: the window server's (app.c sets these), or a string
    // of the window's own
    void (*clipboard_set)(idunn_window_t *win, const char *text, size_t length);
    void (*clipboard_get)(idunn_window_t *win);
    char *clipboard;
    size_t clipboard_length;
    idunn_widget_t *paste_to;

    // a tooltip waits to show over this widget
    idunn_widget_t *tip_widget;
    uint32_t tip_at;
    uint32_t tip_closed; // when the last tooltip went (the next comes at once)

    // a dialog (dialog.c): over the window, it gets all the input
    idunn_widget_t *modal;
    idunn_widget_t *modal_return; // the focus before it

    // popups (popup.c): a window has at most one popup open (a menu, and
    // the menu its sub-menu); a popup has the window it came from
    idunn_window_t *popup;
    idunn_window_t *opener;
    bool transparent;      // a popup: see-through outside its card
    int32_t margin;        // a popup: room for the shadow around the root
    gfx_rect_t anchor;     // a popup: the rect in the opener it is next to
    int side;              // IDUNN_SIDE_
    bool tooltip;
    bool dead;                 // closed: freed by idunn_reap, after the event it came from
    idunn_window_t *next_dead;
    void (*closed)(idunn_window_t *popup); // before a popup goes away
    void *popup_data;
    // make, place and free popups: heimdall's in an app, offscreen ones in tests
    idunn_window_t *(*popup_new)(idunn_window_t *win, int32_t width, int32_t height, bool tooltip);
    void (*popup_place)(idunn_window_t *popup);
    void (*popup_free)(idunn_window_t *popup);

    // for app.c
    idunn_app_t *app;
    void *heim;
    idunn_window_t *next;
};

// the context
bragi_face_t *idunn_face(idunn_context_t *ctx, idunn_text_t style);
const icon_t *idunn_icon(idunn_context_t *ctx, const char *name, int32_t size);
/** a colour icon of the Tela set (files, folders, apps), cached */
const icon_t *idunn_picture(idunn_context_t *ctx, const char *name, int32_t size);

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
/** draw a part of w again (a row of a list), with room for a ring */
void idunn_dirty_rect(idunn_widget_t *w, gfx_rect_t rect);
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

/** send an event to a widget: true if it used it */
bool idunn_send(idunn_widget_t *w, const idunn_event_t *ev);

/** a TIMER event for w at a time (ms, as window->now); one time per widget */
void idunn_wake(idunn_widget_t *w, uint32_t at);

/** put text on the clipboard; ask for its text (a PASTE event comes) */
void idunn_copy(idunn_widget_t *w, const char *text, size_t length);
void idunn_paste(idunn_widget_t *w);

// editable text (text_edit.c)

typedef struct
{
    char *text; // zero-terminated
    size_t length;
    size_t capacity;
    size_t caret;  // byte offsets
    size_t anchor; // the other end of the selection
} idunn_edit_t;

typedef enum
{
    IDUNN_EDIT_IGNORED, // not a key for editing
    IDUNN_EDIT_USED,    // used, nothing changed (a copy)
    IDUNN_EDIT_MOVED,   // the caret or the selection moved
    IDUNN_EDIT_CHANGED, // the text changed
} idunn_edit_result_t;

bool idunn_edit_set(idunn_edit_t *e, const char *text, size_t length);
void idunn_edit_free(idunn_edit_t *e);
size_t idunn_edit_start(const idunn_edit_t *e);
size_t idunn_edit_end(const idunn_edit_t *e);
size_t idunn_edit_prev(const idunn_edit_t *e, size_t pos);
size_t idunn_edit_next(const idunn_edit_t *e, size_t pos);
size_t idunn_edit_word_left(const idunn_edit_t *e, size_t pos);
size_t idunn_edit_word_right(const idunn_edit_t *e, size_t pos);
void idunn_edit_move(idunn_edit_t *e, size_t pos, bool extend);
void idunn_edit_select_word(idunn_edit_t *e, size_t pos);
/** replace the selection with text */
bool idunn_edit_insert(idunn_edit_t *e, const char *text, size_t length);
/** delete the selection, or a character or a word before or after the caret */
void idunn_edit_delete(idunn_edit_t *e, bool forward, bool word);
/** the keys of editing: arrows, Home, End, Backspace, Delete, Ctrl+A/C/X/V, typing */
idunn_edit_result_t idunn_edit_key(idunn_widget_t *w, idunn_edit_t *e, const idunn_event_t *ev,
                                   bool multiline);
/** insert clipboard text (a single line keeps one line) */
bool idunn_edit_paste(idunn_edit_t *e, const char *text, size_t length, bool multiline);

// options: a small control with a label to its right (option.c)
void idunn_option_measure(idunn_widget_t *w, int32_t control_w, int32_t control_h,
                          const char *label, int32_t *width, int32_t *height);
gfx_rect_t idunn_option_control(const idunn_widget_t *w, int32_t control_w, int32_t control_h);
void idunn_option_draw_label(idunn_widget_t *w, gfx_buffer_t *buf, int32_t control_w,
                             const char *label);
/** the border of check boxes and radios (firmer than inputs in dark mode) */
gfx_color_t idunn_option_border(const idunn_theme_t *t);

/**
 * scroll the views w is in so that r (in the window) can be seen: a
 * focused widget, the current row of a list.
 */
void idunn_reveal(idunn_widget_t *w, gfx_rect_t r);

/** true if w and its parents are not hidden */
bool idunn_shown(const idunn_widget_t *w);

// popups (popup.c)
typedef enum
{
    IDUNN_SIDE_BELOW, // as the HEIM_SIDE_ values
    IDUNN_SIDE_ABOVE,
    IDUNN_SIDE_RIGHT,
} idunn_side_t;

/**
 * open a popup window next to anchor (a rect in owner's window) with root
 * in it, margin pixels of room around root for its shadow. a popup that
 * owner's window has open already is closed first.
 * @return the popup, or NULL.
 */
idunn_window_t *idunn_popup_open(idunn_widget_t *owner, idunn_widget_t *root, gfx_rect_t anchor,
                                 idunn_side_t side, int32_t margin, bool tooltip);
/**
 * close a popup and the popups it has open. it is freed by idunn_reap:
 * the event that closed it can still be using it.
 */
void idunn_popup_close(idunn_window_t *popup);
/** free the closed popups (the app loop does it before each event) */
void idunn_reap(idunn_context_t *ctx);
/** close a popup and the popups it came from (a chosen menu item) */
void idunn_popup_close_all(idunn_window_t *popup);
/**
 * open a menu next to anchor (a rect in owner's window), with item current
 * (or -1) as the current one; on_closed (or NULL) is called when it closes.
 */
idunn_window_t *idunn_menu_open(idunn_menu_t *menu, idunn_widget_t *owner, gfx_rect_t anchor,
                                int current, void (*on_closed)(void *data), void *data);

/** give w the keyboard (without the ring) */
void idunn_focus(idunn_widget_t *w);
/** give the keyboard to the first widget that takes it in w */
void idunn_focus_first(idunn_widget_t *w);

/** free w (and its children) after the event that is running (idunn_reap) */
void idunn_free_later(idunn_widget_t *w);

// floating.c
/** show w's tooltip over it */
void idunn_tooltip_show(idunn_widget_t *w);
/** a box with the card look and another shadow (popovers: md, dialogs: window) */
idunn_widget_t *idunn_card_box(int gap, int padding, idunn_shadow_t shadow);
/** close the dialog in win (Escape) */
void idunn_dialog_dismiss(idunn_window_t *win);

/** the well of a text input: fill, inset shadow, border and ring (text_field.c) */
void idunn_draw_well(idunn_widget_t *w, gfx_buffer_t *buf);

/** all the layers of a shadow token around a round box */
void idunn_draw_shadow(const idunn_theme_t *t, gfx_buffer_t *buf, gfx_rect_t box, int32_t radius,
                       idunn_shadow_t which);

/** the 3 px focus ring around a round box */
void idunn_draw_ring(const idunn_widget_t *w, gfx_buffer_t *buf, gfx_rect_t box, int32_t radius);

/** how far drawing goes out of a widget's rect (rings and shadows) */
int32_t idunn_overdraw(const idunn_theme_t *theme);

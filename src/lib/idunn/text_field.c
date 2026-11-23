/**
 * @file text_field.c
 * @brief idunn: a one-line text field: a slightly inset well with a
 *        placeholder, a blinking caret, selection with the mouse and the
 *        keys, the clipboard, and scrolling when the text is longer than
 *        the field.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define HEIGHT    32
#define PADDING   12 // inside the 1 px border
#define PLAIN_PAD 3  // the same, for a plain field
#define ICON_SIZE 16
#define ICON_GAP  8
#define WIDTH     240 // when nothing else gives it a width
#define BLINK_MS  500

typedef struct
{
    idunn_edit_t edit;
    char *placeholder;
    char *icon;
    int32_t scroll;      // pixels of the text left of the field
    uint32_t blink_from; // the caret shows for BLINK_MS from here, then hides
    idunn_callback_t on_change;
    void *change_data;
    idunn_callback_t on_submit;
    void *submit_data;
    idunn_callback_t on_blur;
    void *blur_data;
    bool plain; // a thin box around the text (idunn_text_field_set_plain)
} field_t;

/** where the text goes: the rect inside the padding, after the icon */
static gfx_rect_t text_box(idunn_widget_t *w)
{
    const field_t *f = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    int32_t pad = f->plain ? PLAIN_PAD : PADDING;
    int32_t left = idunn_px(t, pad + 1) + (f->icon ? idunn_px(t, ICON_SIZE + ICON_GAP) : 0);
    int32_t right = idunn_px(t, pad + 1);
    return gfx_rect(w->rect.x + left, w->rect.y, w->rect.w - left - right, w->rect.h);
}

/** the x of a byte offset in the text, from its start */
static int32_t x_of(idunn_widget_t *w, size_t offset)
{
    const field_t *f = w->data;
    bragi_face_t *face = idunn_face(idunn_ctx(w), IDUNN_TEXT_BODY);
    return face ? bragi_caret_x(face, f->edit.text, (int32_t)f->edit.length, (int32_t)offset) : 0;
}

/** the caret place nearest to a window x */
static size_t offset_at(idunn_widget_t *w, int32_t x)
{
    const field_t *f = w->data;
    bragi_face_t *face = idunn_face(idunn_ctx(w), IDUNN_TEXT_BODY);
    gfx_rect_t box = text_box(w);
    return face ? (size_t)bragi_offset_at(face, f->edit.text, (int32_t)f->edit.length,
                                          x - box.x + f->scroll)
                : 0;
}

/** scroll so the caret is in the field, and no further than the text */
static void keep_caret_visible(idunn_widget_t *w)
{
    // not in a window yet: nothing to measure with (its layout does it)
    if (!w->window)
        return;
    field_t *f = w->data;
    int32_t width = text_box(w).w - idunn_px(idunn_theme(w), 1); // room for the caret
    int32_t caret = x_of(w, f->edit.caret);
    int32_t end = x_of(w, f->edit.length);
    if (caret - f->scroll > width)
        f->scroll = caret - width;
    if (caret < f->scroll)
        f->scroll = caret;
    if (end - f->scroll < width)
        f->scroll = end - width;
    if (f->scroll < 0)
        f->scroll = 0;
}

/** the caret shows again at once and starts blinking */
static void restart_blink(idunn_widget_t *w)
{
    field_t *f = w->data;
    f->blink_from = w->window ? w->window->now : 0;
    if (w->state & IDUNN_FOCUSED)
        idunn_wake(w, f->blink_from + BLINK_MS);
}

static void changed(idunn_widget_t *w, bool text_changed)
{
    field_t *f = w->data;
    keep_caret_visible(w);
    restart_blink(w);
    idunn_dirty(w);
    if (text_changed && f->on_change)
        f->on_change(w, f->change_data);
}

static void field_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const field_t *f = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    *width = idunn_px(t, WIDTH);
    *height = f->plain ? idunn_text_style(t, IDUNN_TEXT_BODY).line_height +
                             2 * idunn_px(t, PLAIN_PAD + 1)
                       : idunn_px(t, HEIGHT);
}

static void field_layout(idunn_widget_t *w)
{
    keep_caret_visible(w);
}

void idunn_draw_well(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const idunn_theme_t *t = idunn_theme(w);
    int32_t radius = idunn_radius(t);
    int32_t one = idunn_px(t, 1);
    bool dark = t->mode == IDUNN_DARK;
    bool error = w->state & IDUNN_ERROR;
    gfx_color_t black = gfx_rgb(0, 0, 0);

    // the ring: the accent when focused, red at 25 % with an error
    if (error)
    {
        gfx_shadow_t ring = {0, 0, 0, idunn_px(t, 3),
                             gfx_fade(idunn_color(t, IDUNN_COLOR_DESTRUCTIVE), 64), false};
        gfx_box_shadow(buf, w->rect, radius, &ring);
    }
    else if (idunn_ring_visible(w))
    {
        idunn_draw_ring(w, buf, w->rect, radius);
    }
    else if (!dark)
    {
        // a light line under the well
        gfx_shadow_t under = {0, one, 0, 0, gfx_fade(gfx_rgb(255, 255, 255), 153), false};
        gfx_box_shadow(buf, w->rect, radius, &under);
    }

    gfx_fill_round_rect(buf, w->rect, radius, idunn_color(t, IDUNN_COLOR_FIELD));
    gfx_shadow_t inset = {0, one, 2 * one, 0, gfx_fade(black, dark ? 77 : 10), true};
    gfx_box_shadow(buf, w->rect, radius, &inset);

    idunn_color_t border = error                      ? IDUNN_COLOR_DESTRUCTIVE
                           : (w->state & IDUNN_FOCUSED) ? IDUNN_COLOR_ACCENT
                                                        : IDUNN_COLOR_INPUT;
    gfx_border_round_rect(buf, w->rect, radius, one, idunn_color(t, border));
}

static void field_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    field_t *f = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    if (f->plain)
    {
        // a thin box in the accent, as a name that is being changed in place
        int32_t radius = idunn_px(t, 2);
        gfx_fill_round_rect(buf, w->rect, radius, idunn_color(t, IDUNN_COLOR_FIELD));
        gfx_border_round_rect(buf, w->rect, radius, idunn_px(t, 1),
                              idunn_color(t, IDUNN_COLOR_ACCENT));
    }
    else
        idunn_draw_well(w, buf);

    if (f->icon)
    {
        const icon_t *icon = idunn_icon(ctx, f->icon, ICON_SIZE);
        int32_t size = idunn_px(t, ICON_SIZE);
        if (icon)
            icon_draw_mask(buf, icon, w->rect.x + idunn_px(t, PADDING + 1),
                           w->rect.y + (w->rect.h - size) / 2,
                           idunn_color(t, IDUNN_COLOR_MUTED_FOREGROUND));
    }

    // the text, the selection and the caret, cut to the box
    gfx_rect_t box = text_box(w);
    gfx_rect_t old_clip = buf->clip;
    gfx_set_clip(buf, gfx_rect_intersect(old_clip, box));
    int32_t line = idunn_text_style(t, IDUNN_TEXT_BODY).line_height;
    int32_t top = w->rect.y + (w->rect.h - line) / 2;
    int32_t x = box.x - f->scroll;

    size_t start = idunn_edit_start(&f->edit);
    size_t end = idunn_edit_end(&f->edit);
    bool focused = w->state & IDUNN_FOCUSED;
    if (start != end && focused)
    {
        int32_t from = x + x_of(w, start);
        gfx_fill_rect(buf, gfx_rect(from, top, x + x_of(w, end) - from, line),
                      idunn_color(t, IDUNN_COLOR_SELECTION));
    }
    if (f->edit.length)
        idunn_draw_text(ctx, buf, IDUNN_TEXT_BODY, x, top, 0, f->edit.text,
                        idunn_color(t, IDUNN_COLOR_FOREGROUND));
    else if (f->placeholder)
        idunn_draw_text(ctx, buf, IDUNN_TEXT_BODY, x, top, 0, f->placeholder,
                        idunn_color(t, IDUNN_COLOR_MUTED_FOREGROUND));

    bool shown = ((w->window->now - f->blink_from) / BLINK_MS) % 2 == 0;
    if (focused && shown && !(w->state & IDUNN_DISABLED))
    {
        int32_t one = idunn_px(t, 1);
        gfx_fill_rect(buf, gfx_rect(x + x_of(w, f->edit.caret), top + one, one, line - 2 * one),
                      idunn_color(t, IDUNN_COLOR_FOREGROUND));
    }
    gfx_set_clip(buf, old_clip);
}

static bool field_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    field_t *f = w->data;
    switch (ev->type)
    {
    case IDUNN_EV_KEY:
    {
        if (ev->code == IDUNN_KEY_ENTER)
        {
            if (!f->on_submit)
                return false;
            f->on_submit(w, f->submit_data);
            return true;
        }
        idunn_edit_result_t r = idunn_edit_key(w, &f->edit, ev, false);
        if (r == IDUNN_EDIT_IGNORED)
            return false;
        changed(w, r == IDUNN_EDIT_CHANGED);
        return true;
    }
    case IDUNN_EV_PASTE:
        idunn_edit_paste(&f->edit, ev->text, ev->length, false);
        changed(w, true);
        return true;
    case IDUNN_EV_PRESS:
    {
        size_t at = offset_at(w, ev->x);
        if (ev->clicks == 2)
        {
            idunn_edit_select_word(&f->edit, at);
        }
        else if (ev->clicks == 3)
        {
            idunn_edit_move(&f->edit, 0, false);
            idunn_edit_move(&f->edit, f->edit.length, true);
        }
        else
        {
            idunn_edit_move(&f->edit, at, ev->mods & IDUNN_MOD_SHIFT);
        }
        changed(w, false);
        return true;
    }
    case IDUNN_EV_DRAG:
        idunn_edit_move(&f->edit, offset_at(w, ev->x), true);
        changed(w, false);
        return true;
    case IDUNN_EV_FOCUS:
        restart_blink(w);
        return true;
    case IDUNN_EV_BLUR:
        idunn_dirty(w);
        if (f->on_blur)
            f->on_blur(w, f->blur_data);
        return true;
    case IDUNN_EV_TIMER:
        if (w->state & IDUNN_FOCUSED)
        {
            idunn_wake(w, w->window->now + BLINK_MS);
            idunn_dirty(w);
        }
        return true;
    default:
        return false;
    }
}

static void field_free(idunn_widget_t *w)
{
    field_t *f = w->data;
    idunn_edit_free(&f->edit);
    free(f->placeholder);
    free(f->icon);
    free(f);
}

static const idunn_class_t field_class = {
    .name = "text field",
    .measure = field_measure,
    .layout = field_layout,
    .draw = field_draw,
    .event = field_event,
    .free = field_free,
};

idunn_widget_t *idunn_text_field(const char *placeholder)
{
    field_t *f = calloc(1, sizeof(*f));
    if (!f || !idunn_edit_set(&f->edit, "", 0))
    {
        free(f);
        return NULL;
    }
    f->placeholder = placeholder ? strdup(placeholder) : NULL;
    idunn_widget_t *w =
        idunn_widget_new(&field_class, f, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE | IDUNN_TEXT_INPUT);
    if (!w)
    {
        idunn_edit_free(&f->edit);
        free(f->placeholder);
        free(f);
        return NULL;
    }
    w->cursor = IDUNN_CURSOR_TEXT;
    return w;
}

static field_t *as_field(const idunn_widget_t *w)
{
    return w && w->cls == &field_class ? w->data : NULL;
}

const char *idunn_text_field_text(const idunn_widget_t *field)
{
    const field_t *f = as_field(field);
    return f ? f->edit.text : "";
}

void idunn_text_field_set_text(idunn_widget_t *field, const char *text)
{
    field_t *f = as_field(field);
    if (!f || !idunn_edit_set(&f->edit, text ? text : "", text ? strlen(text) : 0))
        return;
    keep_caret_visible(field);
    idunn_dirty(field);
}

void idunn_text_field_set_icon(idunn_widget_t *field, const char *icon)
{
    field_t *f = as_field(field);
    if (!f)
        return;
    free(f->icon);
    f->icon = icon ? strdup(icon) : NULL;
    idunn_dirty(field);
}

void idunn_text_field_set_plain(idunn_widget_t *field, bool plain)
{
    field_t *f = as_field(field);
    if (!f)
        return;
    f->plain = plain;
    idunn_relayout(field);
}

int32_t idunn_text_field_text_width(idunn_widget_t *field)
{
    field_t *f = as_field(field);
    if (!f || !field->window)
        return 0;
    return x_of(field, f->edit.length);
}

void idunn_text_field_on_change(idunn_widget_t *field, idunn_callback_t on_change, void *data)
{
    field_t *f = as_field(field);
    if (!f)
        return;
    f->on_change = on_change;
    f->change_data = data;
}

void idunn_text_field_on_blur(idunn_widget_t *field, idunn_callback_t on_blur, void *data)
{
    field_t *f = as_field(field);
    if (!f)
        return;
    f->on_blur = on_blur;
    f->blur_data = data;
}

void idunn_text_field_on_submit(idunn_widget_t *field, idunn_callback_t on_submit, void *data)
{
    field_t *f = as_field(field);
    if (!f)
        return;
    f->on_submit = on_submit;
    f->submit_data = data;
}

void idunn_text_field_select(idunn_widget_t *field, size_t start, size_t end)
{
    field_t *f = as_field(field);
    if (!f)
        return;
    f->edit.anchor = start < f->edit.length ? start : f->edit.length;
    f->edit.caret = end < f->edit.length ? end : f->edit.length;
    keep_caret_visible(field);
    idunn_dirty(field);
}

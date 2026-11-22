/**
 * @file text_area.c
 * @brief idunn: a text area: text in lines that wrap at spaces, in the
 *        same well as a text field. Enter makes a new line; Up and Down
 *        keep the caret's column; the text scrolls with the caret and the
 *        wheel.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define PAD_X     12 // inside the 1 px border
#define PAD_Y     8
#define ROWS      4 // lines it shows when nothing else gives it a height
#define WIDTH     240
#define BLINK_MS  500
#define WHEEL     3 // lines a wheel step scrolls

typedef struct
{
    size_t start; // the first byte of the line
    size_t end;   // after its last character (before the spaces or the newline it ends with)
} line_t;

typedef struct
{
    idunn_edit_t edit;
    char *placeholder;
    int rows;
    int32_t scroll;      // pixels of text above the box
    int32_t goal_x;      // the x Up and Down keep (-1: the caret's)
    uint32_t blink_from;
    line_t *lines;       // the wrapped lines, made again after a change
    int line_count;
    int32_t lines_width; // the width they were made for (-1: none)
    idunn_callback_t on_change;
    void *change_data;
} area_t;

static gfx_rect_t text_box(idunn_widget_t *w)
{
    const idunn_theme_t *t = idunn_theme(w);
    int32_t px = idunn_px(t, PAD_X + 1);
    int32_t py = idunn_px(t, PAD_Y + 1);
    return gfx_rect(w->rect.x + px, w->rect.y + py, w->rect.w - 2 * px, w->rect.h - 2 * py);
}

static int32_t line_height(idunn_widget_t *w)
{
    return idunn_text_style(idunn_theme(w), IDUNN_TEXT_BODY).line_height;
}

static void add_line(area_t *a, int *capacity, size_t start, size_t end)
{
    if (a->line_count == *capacity)
    {
        int grown_capacity = *capacity ? *capacity * 2 : 16;
        line_t *grown = realloc(a->lines, (size_t)grown_capacity * sizeof(line_t));
        if (!grown)
            return;
        a->lines = grown;
        *capacity = grown_capacity;
    }
    a->lines[a->line_count++] = (line_t){start, end};
}

/** break the text into lines for the box's width (made once per change) */
static void wrap(idunn_widget_t *w)
{
    area_t *a = w->data;
    int32_t width = text_box(w).w;
    if (a->lines && a->lines_width == width)
        return;
    bragi_face_t *face = idunn_face(idunn_ctx(w), IDUNN_TEXT_BODY);
    free(a->lines);
    a->lines = NULL;
    a->line_count = 0;
    int capacity = 0;
    const char *text = a->edit.text;
    const char *s = text;
    do
    {
        const char *next = s + strlen(s);
        int32_t length = face ? bragi_wrap(face, s, width > 1 ? width : 1, &next)
                              : (int32_t)(next - s);
        add_line(a, &capacity, (size_t)(s - text), (size_t)(s - text) + (size_t)length);
        s = next;
    } while (*s);
    // a newline at the end starts one more, empty line
    if (a->edit.length && text[a->edit.length - 1] == '\n')
        add_line(a, &capacity, a->edit.length, a->edit.length);
    a->lines_width = width;
}

static void text_changed(area_t *a)
{
    a->lines_width = -1;
}

/** the line the caret at offset is on */
static int line_of(const area_t *a, size_t offset)
{
    int i = 0;
    while (i + 1 < a->line_count && a->lines[i + 1].start <= offset)
        i++;
    return i;
}

/** the x of an offset in its line */
static int32_t x_in_line(idunn_widget_t *w, int line, size_t offset)
{
    const area_t *a = w->data;
    bragi_face_t *face = idunn_face(idunn_ctx(w), IDUNN_TEXT_BODY);
    const line_t *l = &a->lines[line];
    size_t at = offset < l->end ? offset : l->end;
    return face ? bragi_caret_x(face, a->edit.text + l->start, (int32_t)(l->end - l->start),
                                (int32_t)(at - l->start))
                : 0;
}

/** the offset nearest to an x in a line */
static size_t offset_in_line(idunn_widget_t *w, int line, int32_t x)
{
    const area_t *a = w->data;
    bragi_face_t *face = idunn_face(idunn_ctx(w), IDUNN_TEXT_BODY);
    const line_t *l = &a->lines[line];
    int32_t at = face ? bragi_offset_at(face, a->edit.text + l->start, (int32_t)(l->end - l->start), x)
                      : 0;
    return l->start + (size_t)at;
}

static size_t offset_at(idunn_widget_t *w, int32_t x, int32_t y)
{
    area_t *a = w->data;
    gfx_rect_t box = text_box(w);
    int line = (y - box.y + a->scroll) / line_height(w);
    line = line < 0 ? 0 : line >= a->line_count ? a->line_count - 1 : line;
    return offset_in_line(w, line, x - box.x);
}

static void keep_caret_visible(idunn_widget_t *w)
{
    area_t *a = w->data;
    wrap(w);
    int32_t lh = line_height(w);
    int32_t height = text_box(w).h;
    int32_t top = line_of(a, a->edit.caret) * lh;
    if (top + lh - a->scroll > height)
        a->scroll = top + lh - height;
    if (top < a->scroll)
        a->scroll = top;
    int32_t most = a->line_count * lh - height;
    if (a->scroll > most)
        a->scroll = most;
    if (a->scroll < 0)
        a->scroll = 0;
}

static void restart_blink(idunn_widget_t *w)
{
    area_t *a = w->data;
    a->blink_from = w->window ? w->window->now : 0;
    if (w->state & IDUNN_FOCUSED)
        idunn_wake(w, a->blink_from + BLINK_MS);
}

static void changed(idunn_widget_t *w, bool text, bool keep_goal)
{
    area_t *a = w->data;
    if (text)
        text_changed(a);
    if (!keep_goal)
        a->goal_x = -1;
    keep_caret_visible(w);
    restart_blink(w);
    idunn_dirty(w);
    if (text && a->on_change)
        a->on_change(w, a->change_data);
}

static void area_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const area_t *a = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    *width = idunn_px(t, WIDTH);
    *height = a->rows * line_height(w) + 2 * idunn_px(t, PAD_Y + 1);
}

static void area_layout(idunn_widget_t *w)
{
    keep_caret_visible(w);
}

static void area_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    area_t *a = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    idunn_draw_well(w, buf);
    wrap(w);

    gfx_rect_t box = text_box(w);
    gfx_rect_t old_clip = buf->clip;
    gfx_set_clip(buf, gfx_rect_intersect(old_clip, box));
    int32_t lh = line_height(w);
    bool focused = w->state & IDUNN_FOCUSED;
    size_t start = idunn_edit_start(&a->edit);
    size_t end = idunn_edit_end(&a->edit);
    int32_t space = idunn_px(t, 4); // a selected line end shows this wide

    if (a->edit.length == 0 && a->placeholder)
        idunn_draw_text(ctx, buf, IDUNN_TEXT_BODY, box.x, box.y, 0, a->placeholder,
                        idunn_color(t, IDUNN_COLOR_MUTED_FOREGROUND));
    int first = a->scroll / lh;
    for (int i = first; i < a->line_count; i++)
    {
        int32_t y = box.y + i * lh - a->scroll;
        if (y >= box.y + box.h)
            break;
        const line_t *l = &a->lines[i];
        size_t next = i + 1 < a->line_count ? a->lines[i + 1].start : a->edit.length;
        if (focused && start != end && start < next && end > l->start)
        {
            int32_t from = x_in_line(w, i, start > l->start ? start : l->start);
            int32_t to = x_in_line(w, i, end);
            if (end >= next && i + 1 < a->line_count)
                to += space; // the selection goes on past the line
            gfx_fill_rect(buf, gfx_rect(box.x + from, y, to - from, lh),
                          idunn_color(t, IDUNN_COLOR_SELECTION));
        }
        if (l->end > l->start)
        {
            char *text = strndup(a->edit.text + l->start, l->end - l->start);
            if (text)
                idunn_draw_text(ctx, buf, IDUNN_TEXT_BODY, box.x, y, 0, text,
                                idunn_color(t, IDUNN_COLOR_FOREGROUND));
            free(text);
        }
    }

    bool shown = ((w->window->now - a->blink_from) / BLINK_MS) % 2 == 0;
    if (focused && shown)
    {
        int line = line_of(a, a->edit.caret);
        int32_t one = idunn_px(t, 1);
        int32_t x = box.x + x_in_line(w, line, a->edit.caret);
        int32_t y = box.y + line * lh - a->scroll;
        gfx_fill_rect(buf, gfx_rect(x, y + one, one, lh - 2 * one),
                      idunn_color(t, IDUNN_COLOR_FOREGROUND));
    }
    gfx_set_clip(buf, old_clip);
}

/** Up, Down, Page Up and Page Down: lines away, at the goal x */
static void move_lines(idunn_widget_t *w, int count, bool extend)
{
    area_t *a = w->data;
    int line = line_of(a, a->edit.caret);
    if (a->goal_x < 0)
        a->goal_x = x_in_line(w, line, a->edit.caret);
    int to = line + count;
    if (to < 0)
        idunn_edit_move(&a->edit, 0, extend);
    else if (to >= a->line_count)
        idunn_edit_move(&a->edit, a->edit.length, extend);
    else
        idunn_edit_move(&a->edit, offset_in_line(w, to, a->goal_x), extend);
}

static bool area_key(idunn_widget_t *w, const idunn_event_t *ev)
{
    area_t *a = w->data;
    bool shift = ev->mods & IDUNN_MOD_SHIFT;
    bool ctrl = ev->mods & IDUNN_MOD_CTRL;
    int page = text_box(w).h / line_height(w);
    wrap(w);
    switch (ev->code)
    {
    case IDUNN_KEY_UP:
    case IDUNN_KEY_DOWN:
    case IDUNN_KEY_PAGEUP:
    case IDUNN_KEY_PAGEDOWN:
    {
        int lines = ev->code == IDUNN_KEY_UP     ? -1
                    : ev->code == IDUNN_KEY_DOWN ? 1
                    : ev->code == IDUNN_KEY_PAGEUP ? -(page > 1 ? page - 1 : 1)
                                                   : (page > 1 ? page - 1 : 1);
        move_lines(w, lines, shift);
        changed(w, false, true);
        return true;
    }
    case IDUNN_KEY_HOME:
    case IDUNN_KEY_END:
        if (!ctrl)
        {
            // the start or the end of the line
            const line_t *l = &a->lines[line_of(a, a->edit.caret)];
            idunn_edit_move(&a->edit, ev->code == IDUNN_KEY_HOME ? l->start : l->end, shift);
            changed(w, false, false);
            return true;
        }
        break; // Ctrl: the whole text, as a text field does
    default:
        break;
    }
    idunn_edit_result_t r = idunn_edit_key(w, &a->edit, ev, true);
    if (r == IDUNN_EDIT_IGNORED)
        return false;
    changed(w, r == IDUNN_EDIT_CHANGED, false);
    return true;
}

static bool area_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    area_t *a = w->data;
    switch (ev->type)
    {
    case IDUNN_EV_KEY:
        return area_key(w, ev);
    case IDUNN_EV_PASTE:
        idunn_edit_paste(&a->edit, ev->text, ev->length, true);
        changed(w, true, false);
        return true;
    case IDUNN_EV_PRESS:
    {
        wrap(w);
        size_t at = offset_at(w, ev->x, ev->y);
        if (ev->clicks == 2)
        {
            idunn_edit_select_word(&a->edit, at);
        }
        else if (ev->clicks == 3)
        {
            idunn_edit_move(&a->edit, 0, false);
            idunn_edit_move(&a->edit, a->edit.length, true);
        }
        else
        {
            idunn_edit_move(&a->edit, at, ev->mods & IDUNN_MOD_SHIFT);
        }
        changed(w, false, false);
        return true;
    }
    case IDUNN_EV_DRAG:
        wrap(w);
        idunn_edit_move(&a->edit, offset_at(w, ev->x, ev->y), true);
        changed(w, false, false);
        return true;
    case IDUNN_EV_SCROLL:
    {
        int32_t before = a->scroll;
        int32_t most = a->line_count * line_height(w) - text_box(w).h;
        a->scroll -= ev->steps * WHEEL * line_height(w);
        a->scroll = a->scroll > most ? most : a->scroll;
        a->scroll = a->scroll < 0 ? 0 : a->scroll;
        if (a->scroll == before)
            return false; // at the end: the parent can scroll
        idunn_dirty(w);
        return true;
    }
    case IDUNN_EV_FOCUS:
        restart_blink(w);
        return true;
    case IDUNN_EV_BLUR:
        idunn_dirty(w);
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

static void free_area(area_t *a)
{
    idunn_edit_free(&a->edit);
    free(a->placeholder);
    free(a->lines);
    free(a);
}

static void area_free(idunn_widget_t *w)
{
    free_area(w->data);
}

static const idunn_class_t area_class = {
    .name = "text area",
    .measure = area_measure,
    .layout = area_layout,
    .draw = area_draw,
    .event = area_event,
    .free = area_free,
};

idunn_widget_t *idunn_text_area(const char *placeholder, int rows)
{
    area_t *a = calloc(1, sizeof(*a));
    if (!a || !idunn_edit_set(&a->edit, "", 0))
    {
        free(a);
        return NULL;
    }
    a->placeholder = placeholder ? strdup(placeholder) : NULL;
    a->rows = rows > 0 ? rows : ROWS;
    a->goal_x = -1;
    a->lines_width = -1;
    idunn_widget_t *w =
        idunn_widget_new(&area_class, a, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE | IDUNN_TEXT_INPUT);
    if (!w)
    {
        free_area(a);
        return NULL;
    }
    w->cursor = IDUNN_CURSOR_TEXT;
    return w;
}

static area_t *as_area(const idunn_widget_t *w)
{
    return w && w->cls == &area_class ? w->data : NULL;
}

const char *idunn_text_area_text(const idunn_widget_t *area)
{
    const area_t *a = as_area(area);
    return a ? a->edit.text : "";
}

void idunn_text_area_set_text(idunn_widget_t *area, const char *text)
{
    area_t *a = as_area(area);
    if (!a || !idunn_edit_set(&a->edit, text ? text : "", text ? strlen(text) : 0))
        return;
    text_changed(a);
    if (area->window)
        keep_caret_visible(area);
    idunn_dirty(area);
}

void idunn_text_area_on_change(idunn_widget_t *area, idunn_callback_t on_change, void *data)
{
    area_t *a = as_area(area);
    if (!a)
        return;
    a->on_change = on_change;
    a->change_data = data;
}

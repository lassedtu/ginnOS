/**
 * @file canvas.c
 * @brief a widget the program draws itself (a terminal, a game).
 *
 * the program gives hooks: draw the part of the window it is told to,
 * take the input, hear of a new size, and (before a frame is drawn) move
 * pixels that only moved. idunn lays the canvas out, gives it the
 * keyboard and the pointer, and sends only the damaged parts.
 */

#include "idunn_internal.h"

#include <stdlib.h>

typedef struct
{
    idunn_canvas_hooks_t hooks;
    void *data;
    int32_t width; // the size the program last heard of
    int32_t height;
} canvas_t;

static void canvas_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)w;
    (void)max_width;
    // its size comes from idunn_set_size or from the space its box gives it
    *width = 0;
    *height = 0;
}

static void canvas_layout(idunn_widget_t *w)
{
    canvas_t *c = w->data;
    if (w->rect.w == c->width && w->rect.h == c->height)
        return;
    c->width = w->rect.w;
    c->height = w->rect.h;
    if (c->hooks.resize)
        c->hooks.resize(w, c->width, c->height, c->data);
}

static void canvas_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    canvas_t *c = w->data;
    if (!c->hooks.draw)
        return;
    gfx_rect_t saved = buf->clip;
    gfx_set_clip(buf, gfx_rect_intersect(saved, w->rect));
    if (!gfx_rect_empty(buf->clip))
        c->hooks.draw(w, buf, w->rect, c->data);
    gfx_set_clip(buf, saved);
}

static void canvas_frame(idunn_widget_t *w)
{
    canvas_t *c = w->data;
    if (c->hooks.frame)
        c->hooks.frame(w, c->data);
}

static bool canvas_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    canvas_t *c = w->data;
    if (!c->hooks.input)
        return false;
    idunn_input_t in = {0};
    switch (ev->type)
    {
    case IDUNN_EV_KEY:
        in.type = IDUNN_INPUT_KEY;
        break;
    case IDUNN_EV_PRESS:
        in.type = IDUNN_INPUT_PRESS;
        break;
    case IDUNN_EV_DRAG:
        in.type = IDUNN_INPUT_DRAG;
        break;
    case IDUNN_EV_RELEASE:
        in.type = IDUNN_INPUT_RELEASE;
        break;
    case IDUNN_EV_MOTION:
        in.type = IDUNN_INPUT_MOTION;
        break;
    case IDUNN_EV_SCROLL:
        in.type = IDUNN_INPUT_SCROLL;
        break;
    case IDUNN_EV_PASTE:
        in.type = IDUNN_INPUT_PASTE;
        break;
    case IDUNN_EV_FOCUS:
        in.type = IDUNN_INPUT_FOCUS;
        break;
    case IDUNN_EV_BLUR:
        in.type = IDUNN_INPUT_BLUR;
        break;
    case IDUNN_EV_TIMER:
        in.type = IDUNN_INPUT_TIMER;
        break;
    default:
        return false;
    }
    in.code = ev->code;
    in.mods = ev->mods;
    in.text = ev->text;
    in.length = ev->length;
    in.x = ev->x - w->rect.x;
    in.y = ev->y - w->rect.y;
    in.clicks = ev->clicks;
    in.steps = ev->steps;
    return c->hooks.input(w, &in, c->data);
}

static void canvas_free(idunn_widget_t *w)
{
    free(w->data);
}

static const idunn_class_t canvas_class = {
    .name = "canvas",
    .measure = canvas_measure,
    .layout = canvas_layout,
    .draw = canvas_draw,
    .event = canvas_event,
    .frame = canvas_frame,
    .free = canvas_free,
};

idunn_widget_t *idunn_canvas(const idunn_canvas_hooks_t *hooks, void *data)
{
    canvas_t *c = calloc(1, sizeof(canvas_t));
    if (!c)
        return NULL;
    c->hooks = *hooks;
    c->data = data;
    c->width = -1;
    c->height = -1;
    idunn_widget_t *w = idunn_widget_new(
        &canvas_class, c, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE | IDUNN_TAKES_TAB | IDUNN_OWN_LOOK);
    if (!w)
    {
        free(c);
        return NULL;
    }
    w->cursor = IDUNN_CURSOR_TEXT;
    return w;
}

static bool is_canvas(const idunn_widget_t *w)
{
    return w && w->cls == &canvas_class;
}

void idunn_canvas_damage(idunn_widget_t *canvas, gfx_rect_t r)
{
    if (!is_canvas(canvas) || !canvas->window)
        return;
    r = gfx_rect(r.x + canvas->rect.x, r.y + canvas->rect.y, r.w, r.h);
    r = gfx_rect_intersect(r, canvas->rect);
    if (!gfx_rect_empty(r))
        idunn_window_add_damage(canvas->window, r);
}

void idunn_canvas_update(idunn_widget_t *canvas)
{
    // one pixel is enough to make a frame, and with it the frame hook
    idunn_canvas_damage(canvas, gfx_rect(0, 0, 1, 1));
}

void idunn_canvas_move(idunn_widget_t *canvas, gfx_rect_t area, int32_t dy)
{
    if (!is_canvas(canvas) || !canvas->window)
        return;
    area = gfx_rect(area.x + canvas->rect.x, area.y + canvas->rect.y, area.w, area.h);
    idunn_window_move_pixels(canvas, gfx_rect_intersect(area, canvas->rect), dy);
}

void idunn_canvas_copy(idunn_widget_t *canvas, const char *text, size_t length)
{
    if (is_canvas(canvas))
        idunn_copy(canvas, text, length);
}

void idunn_canvas_paste(idunn_widget_t *canvas)
{
    if (is_canvas(canvas))
        idunn_paste(canvas);
}

void idunn_canvas_wake(idunn_widget_t *canvas, uint32_t ms)
{
    if (is_canvas(canvas) && canvas->window)
        idunn_wake(canvas, canvas->window->now + ms);
}

void idunn_canvas_set_cursor(idunn_widget_t *canvas, uint32_t shape)
{
    if (is_canvas(canvas))
        canvas->cursor = shape;
}

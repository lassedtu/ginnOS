/**
 * @file view.c
 * @brief Freyja: the canvas that shows the picture.
 *
 * the picture fits in the window when it opens (it is not made larger
 * than 100 %). the wheel zooms about the pointer; a drag moves a picture
 * that is larger than the window; a double click goes between fit and
 * 100 %.
 */

#include "freyja.h"

#include <math.h>

#define ZOOM_MIN   0.01
#define ZOOM_MAX   32.0
#define WHEEL_STEP 1.25
#define MARGIN     16 // design pixels round a fitted picture
#define CHECK      8  // design pixels: the squares behind see-through parts

static struct
{
    int32_t width; // the canvas
    int32_t height;
    double zoom;
    bool fitted;  // the zoom follows the window's size
    double cx;    // the picture point in the middle of the canvas
    double cy;
    bool dragging;
    int32_t drag_x;
    int32_t drag_y;
} V;

static const idunn_theme_t *theme(void)
{
    return idunn_app_theme(F.app);
}

static gfx_color_t token(idunn_color_t c)
{
    const idunn_theme_t *t = theme();
    return t->colors[t->mode][c];
}

void view_redraw(void)
{
    idunn_canvas_damage(F.canvas, gfx_rect(0, 0, V.width, V.height));
}

double view_zoom(void)
{
    return V.zoom;
}

bool view_fitted(void)
{
    return V.fitted;
}

/** keep the picture on the canvas: centred on a side it does not fill */
static void clamp_centre(void)
{
    double w = picture_width(&F.picture);
    double h = picture_height(&F.picture);
    double half_w = V.width / 2.0 / V.zoom;
    double half_h = V.height / 2.0 / V.zoom;
    V.cx = w * V.zoom <= V.width ? w / 2 : fmin(fmax(V.cx, half_w), w - half_w);
    V.cy = h * V.zoom <= V.height ? h / 2 : fmin(fmax(V.cy, half_h), h - half_h);
}

static double fit_zoom(void)
{
    double w = picture_width(&F.picture);
    double h = picture_height(&F.picture);
    if (w <= 0 || h <= 0)
        return 1.0;
    double margin = 2.0 * MARGIN * theme()->scale;
    double room_w = V.width - margin > 1 ? V.width - margin : 1;
    double room_h = V.height - margin > 1 ? V.height - margin : 1;
    double z = fmin(room_w / w, room_h / h);
    return z > 1.0 ? 1.0 : z;
}

void view_fit(void)
{
    V.fitted = true;
    V.zoom = fit_zoom();
    clamp_centre();
    view_redraw();
    freyja_changed();
}

void view_reset(void)
{
    V.cx = picture_width(&F.picture) / 2.0;
    V.cy = picture_height(&F.picture) / 2.0;
    view_fit();
}

/** zoom to z, with the canvas point (x, y) staying on the same picture point */
static void zoom_about(double z, double x, double y)
{
    z = fmin(fmax(z, ZOOM_MIN), ZOOM_MAX);
    double px = V.cx + (x - V.width / 2.0) / V.zoom;
    double py = V.cy + (y - V.height / 2.0) / V.zoom;
    V.zoom = z;
    V.cx = px - (x - V.width / 2.0) / z;
    V.cy = py - (y - V.height / 2.0) / z;
    V.fitted = false;
    clamp_centre();
    view_redraw();
    freyja_changed();
}

void view_zoom_by(double factor)
{
    zoom_about(V.zoom * factor, V.width / 2.0, V.height / 2.0);
}

void view_zoom_to(double zoom)
{
    zoom_about(zoom, V.width / 2.0, V.height / 2.0);
}

static bool larger_than_canvas(void)
{
    return picture_width(&F.picture) * V.zoom > V.width ||
           picture_height(&F.picture) * V.zoom > V.height;
}

static bool input(idunn_widget_t *canvas, const idunn_input_t *in, void *data)
{
    (void)canvas;
    (void)data;
    switch (in->type)
    {
    case IDUNN_INPUT_SCROLL:
        if (F.picture.level_count)
            zoom_about(V.zoom * pow(WHEEL_STEP, in->steps), in->x, in->y);
        return true;
    case IDUNN_INPUT_PRESS:
        idunn_set_focus(F.canvas);
        if (!F.picture.level_count)
            return true;
        if (in->clicks == 2)
        {
            if (V.fitted && fabs(V.zoom - 1.0) > 1e-9)
                zoom_about(1.0, in->x, in->y);
            else
                view_fit();
            return true;
        }
        V.dragging = larger_than_canvas();
        V.drag_x = in->x;
        V.drag_y = in->y;
        return true;
    case IDUNN_INPUT_DRAG:
        if (V.dragging)
        {
            V.cx -= (in->x - V.drag_x) / V.zoom;
            V.cy -= (in->y - V.drag_y) / V.zoom;
            V.drag_x = in->x;
            V.drag_y = in->y;
            clamp_centre();
            view_redraw();
        }
        return true;
    case IDUNN_INPUT_RELEASE:
        V.dragging = false;
        return true;
    default:
        return false;
    }
}

static void draw(idunn_widget_t *canvas, gfx_buffer_t *buf, gfx_rect_t rect, void *data)
{
    (void)canvas;
    (void)data;
    gfx_fill_rect(buf, rect, token(IDUNN_COLOR_MUTED));
    if (!F.picture.level_count)
        return;
    double x = rect.x + V.width / 2.0 - V.cx * V.zoom;
    double y = rect.y + V.height / 2.0 - V.cy * V.zoom;
    gfx_rect_t saved = buf->clip;
    buf->clip = gfx_rect_intersect(saved, rect);
    bool dark = theme()->mode == IDUNN_DARK;
    picture_draw(&F.picture, buf, floor(x), floor(y), V.zoom,
                 dark ? gfx_rgb(0x3A, 0x3A, 0x3E) : gfx_rgb(0xFF, 0xFF, 0xFF),
                 dark ? gfx_rgb(0x2C, 0x2C, 0x30) : gfx_rgb(0xE4, 0xE4, 0xE8),
                 CHECK * theme()->scale);
    buf->clip = saved;
}

static void resize(idunn_widget_t *canvas, int32_t width, int32_t height, void *data)
{
    (void)canvas;
    (void)data;
    V.width = width;
    V.height = height;
    if (V.fitted)
        V.zoom = fit_zoom();
    clamp_centre();
    freyja_changed();
}

idunn_widget_t *view_new(void)
{
    V.zoom = 1.0;
    V.fitted = true;
    static const idunn_canvas_hooks_t hooks = {draw, input, resize, NULL};
    F.canvas = idunn_canvas(&hooks, NULL);
    return F.canvas;
}

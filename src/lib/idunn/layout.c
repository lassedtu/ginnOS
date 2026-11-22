/**
 * @file layout.c
 * @brief idunn: rows and columns (a small CSS flexbox) and spacers.
 *
 * a box measures its children, gives the free space along it to the
 * children that grow (or, when there is too little, takes it from all of
 * them in proportion to their size, as flex-shrink does), and places
 * them with the gap between them. across the box a child is stretched or
 * aligned.
 */

#include "idunn_internal.h"

#include <stdlib.h>

#define UNIT 4 // design pixels in a space unit

typedef struct
{
    bool column;
    int gap;              // units
    int pad_v;            // units
    int pad_h;
    idunn_align_t align;   // across
    idunn_align_t justify; // along
    int background;       // a colour token, or -1
} box_t;

static box_t *box_of(idunn_widget_t *w)
{
    return w->data;
}

/** the size of a child along the box and across it */
static void child_size(idunn_widget_t *child, bool column, int32_t max_width, int32_t *along,
                       int32_t *across)
{
    int32_t w;
    int32_t h;
    idunn_measure(child, max_width, &w, &h);
    *along = column ? h : w;
    *across = column ? w : h;
}

static void box_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    const box_t *b = box_of(w);
    const idunn_theme_t *t = idunn_theme(w);
    int32_t pad_v = idunn_px(t, b->pad_v * UNIT);
    int32_t pad_h = idunn_px(t, b->pad_h * UNIT);
    int32_t gap = idunn_px(t, b->gap * UNIT);
    int32_t inner = max_width > 0 ? max_width - 2 * pad_h : 0;
    if (max_width > 0 && inner < 1)
        inner = 1;

    int32_t along = 0;
    int32_t across = 0;
    for (int i = 0; i < w->child_count; i++)
    {
        int32_t a;
        int32_t c;
        child_size(w->children[i], b->column, inner, &a, &c);
        along += a + (i > 0 ? gap : 0);
        if (c > across)
            across = c;
    }
    *width = (b->column ? across : along) + 2 * pad_h;
    *height = (b->column ? along : across) + 2 * pad_v;
}

/** where a size goes in a space: at the start, in the middle or at the end */
static int32_t aligned(idunn_align_t align, int32_t space, int32_t size)
{
    if (align == IDUNN_CENTER)
        return (space - size) / 2;
    if (align == IDUNN_END)
        return space - size;
    return 0;
}

static void box_layout(idunn_widget_t *w)
{
    const box_t *b = box_of(w);
    int n = w->child_count;
    if (n == 0)
        return;
    const idunn_theme_t *t = idunn_theme(w);
    int32_t pad_v = idunn_px(t, b->pad_v * UNIT);
    int32_t pad_h = idunn_px(t, b->pad_h * UNIT);
    int32_t gap = idunn_px(t, b->gap * UNIT);
    gfx_rect_t inner = gfx_rect(w->rect.x + pad_h, w->rect.y + pad_v, w->rect.w - 2 * pad_h,
                                w->rect.h - 2 * pad_v);
    int32_t main = b->column ? inner.h : inner.w;
    int32_t cross = b->column ? inner.w : inner.h;

    int32_t *along = malloc((size_t)n * 2 * sizeof(int32_t));
    if (!along)
        return;
    int32_t *across = along + n;
    int32_t total = 0;
    int grow = 0;
    for (int i = 0; i < n; i++)
    {
        child_size(w->children[i], b->column, inner.w, &along[i], &across[i]);
        total += along[i];
        grow += w->children[i]->grow;
    }
    int32_t free_space = main - total - gap * (n - 1);

    // give the free space to the children that grow, or take what is
    // missing from all of them
    if (free_space > 0 && grow > 0)
    {
        int32_t given = 0;
        int seen = 0;
        for (int i = 0; i < n; i++)
        {
            if (!w->children[i]->grow)
                continue;
            seen += w->children[i]->grow;
            int32_t share = (int32_t)((int64_t)free_space * seen / grow) - given;
            along[i] += share;
            given += share;
        }
        free_space = 0;
    }
    else if (free_space < 0 && total > 0)
    {
        int32_t missing = -free_space;
        int32_t taken = 0;
        int64_t seen = 0;
        for (int i = 0; i < n; i++)
        {
            seen += along[i];
            int32_t share = (int32_t)(missing * seen / total) - taken;
            along[i] -= share;
            taken += share;
        }
        free_space = 0;
    }

    int32_t pos = aligned(b->justify, free_space, 0);
    int32_t extra = b->justify == IDUNN_SPACE_BETWEEN && n > 1 ? free_space / (n - 1) : 0;
    for (int i = 0; i < n; i++)
    {
        int32_t size = b->align == IDUNN_STRETCH || across[i] > cross ? cross : across[i];
        int32_t off = b->align == IDUNN_STRETCH ? 0 : aligned(b->align, cross, size);
        gfx_rect_t r = b->column ? gfx_rect(inner.x + off, inner.y + pos, size, along[i])
                                 : gfx_rect(inner.x + pos, inner.y + off, along[i], size);
        idunn_place(w->children[i], r);
        pos += along[i] + gap + extra;
    }
    free(along);
}

static void box_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const box_t *b = box_of(w);
    if (b->background >= 0)
        gfx_fill_rect(buf, w->rect, idunn_color(idunn_theme(w), (idunn_color_t)b->background));
}

static void box_free(idunn_widget_t *w)
{
    free(w->data);
}

static const idunn_class_t box_class = {
    .name = "box",
    .measure = box_measure,
    .layout = box_layout,
    .draw = box_draw,
    .free = box_free,
};

static idunn_widget_t *box_new(bool column, int gap, int padding)
{
    box_t *b = malloc(sizeof(*b));
    if (!b)
        return NULL;
    *b = (box_t){column, gap, padding, padding, IDUNN_STRETCH, IDUNN_START, -1};
    idunn_widget_t *w = idunn_widget_new(&box_class, b, 0);
    if (!w)
        free(b);
    return w;
}

idunn_widget_t *idunn_row(int gap, int padding)
{
    return box_new(false, gap, padding);
}

idunn_widget_t *idunn_column(int gap, int padding)
{
    return box_new(true, gap, padding);
}

/** the box data of w, or NULL if w is not a box */
static box_t *as_box(idunn_widget_t *w)
{
    return w && w->cls == &box_class ? w->data : NULL;
}

bool idunn_is_column(const idunn_widget_t *w)
{
    return w && w->cls == &box_class && ((const box_t *)w->data)->column;
}

void idunn_box_set_align(idunn_widget_t *box, idunn_align_t align, idunn_align_t justify)
{
    box_t *b = as_box(box);
    if (!b)
        return;
    b->align = align;
    b->justify = justify;
    idunn_relayout(box);
}

void idunn_box_set_padding(idunn_widget_t *box, int vertical, int horizontal)
{
    box_t *b = as_box(box);
    if (!b)
        return;
    b->pad_v = vertical;
    b->pad_h = horizontal;
    idunn_relayout(box);
}

void idunn_box_set_background(idunn_widget_t *box, idunn_color_t color)
{
    box_t *b = as_box(box);
    if (!b)
        return;
    b->background = color;
    idunn_dirty(box);
}

static const idunn_class_t spacer_class = {.name = "spacer"};

idunn_widget_t *idunn_spacer(void)
{
    idunn_widget_t *w = idunn_widget_new(&spacer_class, NULL, 0);
    if (w)
        w->grow = 1;
    return w;
}

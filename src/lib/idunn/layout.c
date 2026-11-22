/**
 * @file layout.c
 * @brief idunn: rows and columns (a small CSS flexbox) and spacers.
 *
 * a box measures its children, gives the free space along it to the
 * children that grow, and places them with the gap between them. across
 * the box a child is stretched or aligned.
 *
 * when a row is too narrow, the children that can shrink (text and the
 * boxes it is in) give up room in proportion to their size, as
 * flex-shrink does. nothing else shrinks, as in CSS, where an item is not
 * made smaller than its content: a column that is too short overflows.
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
    bool card;            // the card look: a border, a large radius, a shadow
    bool keep_end;        // too little room: the first children go out of view
    idunn_shadow_t shadow; // the card's shadow
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
    int shown = 0;
    for (int i = 0; i < w->child_count; i++)
    {
        if (w->children[i]->state & IDUNN_HIDDEN)
            continue;
        int32_t a;
        int32_t c;
        child_size(w->children[i], b->column, inner, &a, &c);
        along += a + (shown++ > 0 ? gap : 0);
        if (c > across)
            across = c;
    }
    *width = (b->column ? across : along) + 2 * pad_h;
    *height = (b->column ? along : across) + 2 * pad_v;
}

/** a child can give up room along a row: text and boxes, unless their width is fixed */
static bool can_shrink(const idunn_widget_t *child)
{
    return (child->flags & IDUNN_SHRINKS) && child->width == 0;
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
    // the children that take part (hidden ones do not)
    idunn_widget_t **kids = malloc((size_t)(w->child_count ? w->child_count : 1) * sizeof(*kids));
    if (!kids)
        return;
    int n = 0;
    for (int i = 0; i < w->child_count; i++)
    {
        if (!(w->children[i]->state & IDUNN_HIDDEN))
            kids[n++] = w->children[i];
    }
    if (n == 0)
    {
        free(kids);
        return;
    }
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
    {
        free(kids);
        return;
    }
    int32_t *across = along + n;
    int32_t total = 0;
    int32_t shrinkable = 0;
    int grow = 0;
    for (int i = 0; i < n; i++)
    {
        child_size(kids[i], b->column, inner.w, &along[i], &across[i]);
        total += along[i];
        grow += kids[i]->grow;
        if (can_shrink(kids[i]))
            shrinkable += along[i];
    }
    int32_t free_space = main - total - gap * (n - 1);

    // give the free space to the children that grow, or take what is
    // missing from the ones that can shrink
    if (free_space > 0 && grow > 0)
    {
        int32_t given = 0;
        int seen = 0;
        for (int i = 0; i < n; i++)
        {
            if (!kids[i]->grow)
                continue;
            seen += kids[i]->grow;
            int32_t share = (int32_t)((int64_t)free_space * seen / grow) - given;
            along[i] += share;
            given += share;
        }
        free_space = 0;
    }
    else if (free_space < 0 && !b->column && shrinkable > 0)
    {
        int32_t missing = -free_space < shrinkable ? -free_space : shrinkable;
        int32_t taken = 0;
        int64_t seen = 0;
        for (int i = 0; i < n; i++)
        {
            if (!can_shrink(kids[i]))
                continue;
            seen += along[i];
            int32_t share = (int32_t)(missing * seen / shrinkable) - taken;
            along[i] -= share;
            taken += share;
        }
        free_space = 0;
    }

    int32_t pos = aligned(b->justify, free_space, 0);
    // still too long: start so far to the left that the last children fit,
    // and whole children go out of view (the box clips them)
    if (b->keep_end && free_space < 0)
    {
        int32_t length = total + gap * (n - 1);
        pos = 0;
        for (int i = 0; i < n - 1 && length > main; i++)
        {
            pos -= along[i] + gap;
            length -= along[i] + gap;
        }
    }
    int32_t extra = b->justify == IDUNN_SPACE_BETWEEN && n > 1 ? free_space / (n - 1) : 0;
    for (int i = 0; i < n; i++)
    {
        int32_t size = b->align == IDUNN_STRETCH || across[i] > cross ? cross : across[i];
        int32_t off = b->align == IDUNN_STRETCH ? 0 : aligned(b->align, cross, size);
        gfx_rect_t r = b->column ? gfx_rect(inner.x + off, inner.y + pos, size, along[i])
                                 : gfx_rect(inner.x + pos, inner.y + off, along[i], size);
        idunn_place(kids[i], r);
        pos += along[i] + gap + extra;
    }
    free(along);
    free(kids);
}

static void box_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const box_t *b = box_of(w);
    const idunn_theme_t *t = idunn_theme(w);
    if (b->card)
    {
        int32_t radius = idunn_radius_lg(t);
        idunn_draw_shadow(t, buf, w->rect, radius, b->shadow);
        gfx_fill_round_rect(buf, w->rect, radius, idunn_color(t, IDUNN_COLOR_CARD));
        gfx_border_round_rect(buf, w->rect, radius, idunn_px(t, 1),
                              idunn_color(t, IDUNN_COLOR_BORDER));
        return;
    }
    if (b->background >= 0)
        gfx_fill_rect(buf, w->rect, idunn_color(t, (idunn_color_t)b->background));
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
    *b = (box_t){.column = column,
                 .gap = gap,
                 .pad_v = padding,
                 .pad_h = padding,
                 .align = IDUNN_STRETCH,
                 .justify = IDUNN_START,
                 .background = -1,
                 .shadow = IDUNN_SHADOW_XS};
    idunn_widget_t *w = idunn_widget_new(&box_class, b, IDUNN_SHRINKS);
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

idunn_widget_t *idunn_card_box(int gap, int padding, idunn_shadow_t shadow)
{
    idunn_widget_t *w = box_new(true, gap, padding);
    if (w)
    {
        box_of(w)->card = true;
        box_of(w)->shadow = shadow;
    }
    return w;
}

idunn_widget_t *idunn_card(int gap)
{
    return idunn_card_box(gap, 6, IDUNN_SHADOW_XS);
}

void idunn_card_header(idunn_widget_t *card, const char *title, const char *description)
{
    idunn_widget_t *head = idunn_add(card, idunn_column(1, 0));
    idunn_add(head, idunn_label(title ? title : "", IDUNN_TEXT_TITLE));
    if (description)
    {
        idunn_widget_t *d = idunn_add(head, idunn_label(description, IDUNN_TEXT_BODY));
        idunn_label_set_color(d, IDUNN_COLOR_MUTED_FOREGROUND);
        idunn_label_set_wrap(d, true);
    }
}

static const idunn_class_t spacer_class = {.name = "spacer"};

idunn_widget_t *idunn_spacer(void)
{
    idunn_widget_t *w = idunn_widget_new(&spacer_class, NULL, 0);
    if (w)
        w->grow = 1;
    return w;
}

void idunn_box_set_keep_end(idunn_widget_t *box, bool keep_end)
{
    box_t *b = as_box(box);
    if (!b)
        return;
    b->keep_end = keep_end;
    if (keep_end)
        box->flags |= IDUNN_CLIPS;
    else
        box->flags &= ~IDUNN_CLIPS;
    idunn_relayout(box);
}

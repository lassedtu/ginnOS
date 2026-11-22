/**
 * @file widgets_test.c
 * @brief host tests of idunn's widgets: layout, input, focus and drawing.
 */

#include "../idunn_internal.h"
#include "hosttest.h"

#include "image.h"

#include <stdio.h>
#include <stdlib.h>

void test_widgets(void);
void compare_reference(const char *name, const gfx_buffer_t *buf, double mean_limit);
void test_inputs(idunn_context_t *ctx);
void test_lists(idunn_context_t *ctx);

static idunn_context_t *ctx;
static int clicks;

static void count_click(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    clicks++;
}

static bool rect_is(gfx_rect_t r, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void test_layout(void)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 400, 200);
    idunn_widget_t *col = idunn_column(2, 4);
    idunn_widget_t *title = idunn_add(col, idunn_label("Rename file", IDUNN_TEXT_HEADING));
    idunn_widget_t *row = idunn_add(col, idunn_row(2, 0));
    idunn_widget_t *cancel = idunn_add(row, idunn_button("Cancel", IDUNN_OUTLINE, NULL, NULL));
    idunn_add(row, idunn_spacer());
    idunn_widget_t *ok = idunn_add(row, idunn_button("OK", IDUNN_PRIMARY, NULL, NULL));
    idunn_widget_t *line = idunn_add(col, idunn_separator());
    idunn_window_set_root(win, col);
    idunn_window_draw(win);

    // padding 16, gap 8; a heading line is 26 high and a button 32
    CHECK(rect_is(col->rect, 0, 0, 400, 200));
    CHECK(rect_is(title->rect, 16, 16, 368, 26));
    CHECK(rect_is(row->rect, 16, 50, 368, 32));
    CHECK(rect_is(line->rect, 16, 90, 368, 1));

    // a button is its text, 16 px padding and a 1 px border on each side;
    // the spacer pushes OK right
    bragi_face_t *face = idunn_face(ctx, IDUNN_TEXT_LABEL);
    CHECK_EQ(cancel->rect.w, bragi_caret_x(face, "Cancel", 6, 6) + 34);
    CHECK_EQ(cancel->rect.x, 16);
    CHECK_EQ(ok->rect.x + ok->rect.w, 384);
    CHECK_EQ(ok->rect.h, 32);

    // grow shares the free space by weight
    idunn_widget_t *r2 = idunn_row(0, 0);
    idunn_widget_t *a = idunn_grow(idunn_add(r2, idunn_spacer()), 1);
    idunn_widget_t *b = idunn_grow(idunn_add(r2, idunn_spacer()), 2);
    idunn_window_set_root(win, r2);
    idunn_window_draw(win);
    CHECK_EQ(a->rect.w, 133);
    CHECK_EQ(b->rect.w, 267);
    CHECK_EQ(b->rect.x, 133);

    // justify and align: in the middle both ways
    idunn_widget_t *r3 = idunn_row(0, 0);
    idunn_box_set_align(r3, IDUNN_CENTER, IDUNN_CENTER);
    idunn_widget_t *icon = idunn_add(r3, idunn_icon_button("x", IDUNN_GHOST, NULL, NULL));
    idunn_window_set_root(win, r3);
    idunn_window_draw(win);
    CHECK(rect_is(icon->rect, 184, 84, 32, 32));

    // space between: the first at the start, the last at the end
    idunn_box_set_align(r3, IDUNN_START, IDUNN_SPACE_BETWEEN);
    idunn_widget_t *second = idunn_add(r3, idunn_icon_button("x", IDUNN_GHOST, NULL, NULL));
    idunn_window_draw(win);
    CHECK_EQ(icon->rect.x, 0);
    CHECK_EQ(second->rect.x, 368);
    CHECK_EQ(icon->rect.y, 0);

    // too little room in a row: text gives it up (and ends with "..."),
    // a button does not
    idunn_window_resize(win, 100, 40);
    idunn_widget_t *r4 = idunn_row(0, 0);
    idunn_widget_t *long_label = idunn_add(r4, idunn_label("A long line of text", IDUNN_TEXT_BODY));
    idunn_widget_t *fixed = idunn_add(r4, idunn_button("Keep", IDUNN_OUTLINE, NULL, NULL));
    idunn_window_set_root(win, r4);
    idunn_window_draw(win);
    int32_t keep_width;
    int32_t keep_height;
    idunn_measure(fixed, 0, &keep_width, &keep_height);
    CHECK_EQ(fixed->rect.w, keep_width);
    CHECK_EQ(long_label->rect.w, 100 - keep_width);

    // a box with a fixed width keeps it (a side bar)
    idunn_widget_t *r6 = idunn_row(0, 0);
    idunn_widget_t *side = idunn_add(r6, idunn_column(0, 0));
    idunn_set_size(side, 40, 0);
    idunn_widget_t *rest = idunn_add(r6, idunn_column(0, 0));
    idunn_add(rest, idunn_label("A long line of text", IDUNN_TEXT_BODY));
    idunn_window_set_root(win, r6);
    idunn_window_draw(win);
    CHECK_EQ(side->rect.w, 40);
    CHECK_EQ(rest->rect.w, 60);

    // a column that is too short does not squash its children
    idunn_widget_t *c4 = idunn_column(0, 0);
    idunn_widget_t *b1 = idunn_add(c4, idunn_button("One", IDUNN_OUTLINE, NULL, NULL));
    idunn_widget_t *b2 = idunn_add(c4, idunn_button("Two", IDUNN_OUTLINE, NULL, NULL));
    idunn_window_set_root(win, c4);
    idunn_window_draw(win);
    CHECK_EQ(b1->rect.h, 32);
    CHECK_EQ(b2->rect.y, 32);

    // a wrapping label is as high as its lines
    idunn_window_resize(win, 60, 200);
    idunn_widget_t *c5 = idunn_column(0, 0);
    idunn_widget_t *para = idunn_add(c5, idunn_label("one two three four five six", IDUNN_TEXT_BODY));
    idunn_label_set_wrap(para, true);
    idunn_window_set_root(win, c5);
    idunn_window_draw(win);
    CHECK(para->rect.h >= 3 * 18);
    CHECK_EQ(para->rect.h % 18, 0);

    // a fixed size wins
    idunn_set_size(para, 30, 10);
    idunn_window_draw(win);
    CHECK_EQ(para->rect.h, 10);

    idunn_window_free_widgets(win);
    free(win);
}

static void test_input(void)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 100);
    idunn_widget_t *row = idunn_row(2, 4);
    idunn_widget_t *one = idunn_add(row, idunn_button("One", IDUNN_PRIMARY, count_click, NULL));
    idunn_widget_t *off = idunn_add(row, idunn_button("Off", IDUNN_OUTLINE, count_click, NULL));
    idunn_widget_t *two = idunn_add(row, idunn_button("Two", IDUNN_GHOST, count_click, NULL));
    idunn_set_disabled(off, true);
    idunn_window_set_root(win, row);
    idunn_window_draw(win);
    gfx_rect_t r = one->rect;

    // hover, then a click
    clicks = 0;
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, r.x + 4, r.y + 4, 0);
    CHECK(one->state & IDUNN_HOVER);
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, r.x + 4, r.y + 4, 0);
    CHECK(one->state & IDUNN_PRESSED);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, r.x + 4, r.y + 4, 0);
    CHECK_EQ(clicks, 1);
    CHECK(!(one->state & IDUNN_PRESSED));

    // a click takes the focus, without the ring
    CHECK(idunn_window_focus(win) == one);
    CHECK(!idunn_ring_visible(one));

    // pressed, then let go somewhere else: no click
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, r.x + 4, r.y + 4, 0);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, 299, 99, 0);
    CHECK(!(one->state & IDUNN_HOVER));
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, 299, 99, 0);
    CHECK_EQ(clicks, 1);

    // a disabled button gets nothing
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, off->rect.x + 4, off->rect.y + 4, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, off->rect.x + 4, off->rect.y + 4, 0);
    CHECK_EQ(clicks, 1);
    CHECK(!(off->state & IDUNN_HOVER));
    CHECK(idunn_window_focus(win) == NULL);

    // Tab goes around the buttons that can take it, and shows the ring
    idunn_window_key(win, IDUNN_KEY_TAB, 0, "");
    CHECK(idunn_window_focus(win) == one);
    CHECK(idunn_ring_visible(one));
    idunn_window_key(win, IDUNN_KEY_TAB, 0, "");
    CHECK(idunn_window_focus(win) == two);
    idunn_window_key(win, IDUNN_KEY_TAB, 0, "");
    CHECK(idunn_window_focus(win) == one);
    idunn_window_key(win, IDUNN_KEY_TAB, IDUNN_MOD_SHIFT, "");
    CHECK(idunn_window_focus(win) == two);

    // Enter and Space click the focused button
    idunn_window_key(win, IDUNN_KEY_ENTER, 0, "\n");
    idunn_window_key(win, IDUNN_KEY_SPACE, 0, " ");
    CHECK_EQ(clicks, 3);
    idunn_window_key(win, IDUNN_KEY_A, 0, "a");
    CHECK_EQ(clicks, 3);

    // the pointer leaves the window
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, two->rect.x + 2, two->rect.y + 2, 0);
    CHECK(two->state & IDUNN_HOVER);
    idunn_window_pointer(win, IDUNN_POINTER_LEAVE, 0, 0, 0);
    CHECK(!(two->state & IDUNN_HOVER));

    // freeing the focused widget forgets it
    idunn_widget_free(two);
    CHECK(idunn_window_focus(win) == NULL);
    CHECK_EQ(row->child_count, 2);

    idunn_window_free_widgets(win);
    free(win);
}

static void test_drawing(void)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 100);
    idunn_widget_t *row = idunn_row(2, 4);
    idunn_widget_t *b = idunn_add(row, idunn_button("Save", IDUNN_PRIMARY, NULL, NULL));
    idunn_window_set_root(win, row);
    gfx_rect_t first = idunn_window_draw(win);
    CHECK(rect_is(first, 0, 0, 300, 100));
    CHECK(gfx_rect_empty(idunn_window_draw(win)));

    // hover is a 100 ms transition: frames until it ends, each one only
    // around the button
    idunn_window_tick(win, 1000);
    gfx_color_t before = *gfx_pixel_at(win->buffer, b->rect.x + 8, b->rect.y + 20);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, b->rect.x + 2, b->rect.y + 2, 0);
    gfx_rect_t damage = idunn_window_draw(win);
    CHECK(damage.w < 300 && damage.w >= b->rect.w);
    idunn_window_tick(win, 1050);
    idunn_window_draw(win);
    CHECK(idunn_window_tick(win, 1050)); // still running at 50 ms
    gfx_color_t middle = *gfx_pixel_at(win->buffer, b->rect.x + 8, b->rect.y + 20);
    CHECK(middle != before);
    idunn_window_tick(win, 1100);
    idunn_window_draw(win);
    CHECK(!idunn_window_tick(win, 1100));
    gfx_color_t after = *gfx_pixel_at(win->buffer, b->rect.x + 8, b->rect.y + 20);
    CHECK(after != middle);
    CHECK(gfx_rect_empty(idunn_window_draw(win)));

    // with motion off it changes at once
    idunn_theme_t theme = ctx->theme;
    theme.motion = false;
    idunn_context_set_theme(ctx, &theme);
    idunn_window_pointer(win, IDUNN_POINTER_LEAVE, 0, 0, 0);
    idunn_window_draw(win);
    CHECK(!idunn_window_tick(win, 1101));
    CHECK_EQ(*gfx_pixel_at(win->buffer, b->rect.x + 8, b->rect.y + 20), before);
    theme.motion = true;
    idunn_context_set_theme(ctx, &theme);

    idunn_window_free_widgets(win);
    free(win);
}

static uint8_t *rgb_of(const gfx_buffer_t *buf)
{
    uint8_t *rgb = malloc((size_t)buf->width * buf->height * 3);
    for (int32_t i = 0; i < buf->width * buf->height; i++)
    {
        uint32_t p = *gfx_pixel_at(buf, i % buf->width, i / buf->width);
        rgb[i * 3] = (uint8_t)(p >> 16);
        rgb[i * 3 + 1] = (uint8_t)(p >> 8);
        rgb[i * 3 + 2] = (uint8_t)p;
    }
    return rgb;
}

/**
 * compare a drawing with its reference cell (test/reference/<name>.png,
 * the preview's CSS drawn by Chrome: tools/idunn-ref/spec.py).
 */
void compare_reference(const char *name, const gfx_buffer_t *buf, double mean_limit)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/reference/%s.png", TEST_DIR, name);
    gfx_buffer_t *ref = image_load(path, NULL);
    bool same_size = ref && ref->width == buf->width && ref->height == buf->height;
    CHECK(same_size);
    if (same_size)
    {
        uint8_t *got = rgb_of(buf);
        uint8_t *want = rgb_of(ref);
        hosttest_compare(name, got, want, buf->width, buf->height, 255, mean_limit);
        free(got);
        free(want);
    }
    gfx_buffer_free(ref);
}

/**
 * the button variants in one mode, compared with the preview's CSS drawn
 * by Chrome (test/reference, tools/idunn-ref/spec.py).
 */
static void reference_buttons(const char *name, idunn_mode_t mode, double mean_limit)
{
    idunn_theme_t theme;
    idunn_theme_default(&theme);
    theme.mode = mode;
    idunn_context_set_theme(ctx, &theme);

    idunn_window_t *win = idunn_window_offscreen(ctx, 520, 64);
    idunn_widget_t *row = idunn_row(2, 4);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
    idunn_add(row, idunn_button("Primary", IDUNN_PRIMARY, NULL, NULL));
    idunn_add(row, idunn_button("Secondary", IDUNN_SECONDARY, NULL, NULL));
    idunn_add(row, idunn_button("Outline", IDUNN_OUTLINE, NULL, NULL));
    idunn_add(row, idunn_button("Ghost", IDUNN_GHOST, NULL, NULL));
    idunn_add(row, idunn_button("Delete", IDUNN_DESTRUCTIVE, NULL, NULL));
    idunn_add(row, idunn_button("Link", IDUNN_LINK, NULL, NULL));
    idunn_window_set_root(win, row);
    idunn_window_draw(win);
    compare_reference(name, win->buffer, mean_limit);
    idunn_window_free_widgets(win);
    free(win);
}

void test_widgets(void)
{
    idunn_theme_t theme;
    idunn_theme_default(&theme);
    ctx = idunn_context_new(&theme, FONT_DIR, PACK_DIR "/lucide.pack");
    CHECK(ctx != NULL);
    if (!ctx)
        return;
    CHECK(idunn_icon(ctx, "x", 16) != NULL);
    test_layout();
    test_input();
    test_drawing();
    test_inputs(ctx);
    test_lists(ctx);
    reference_buttons("buttons-dark", IDUNN_DARK, 5.0);
    reference_buttons("buttons-light", IDUNN_LIGHT, 5.0);
    idunn_context_free(ctx);
}

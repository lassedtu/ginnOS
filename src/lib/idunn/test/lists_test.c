/**
 * @file lists_test.c
 * @brief host tests of idunn's structure: scroll views, cards, tabs and
 *        lists.
 */

#include "../idunn_internal.h"
#include "hosttest.h"

#include <stdlib.h>
#include <string.h>

void test_lists(idunn_context_t *ctx);

static void key(idunn_window_t *win, uint16_t code, uint32_t mods)
{
    idunn_window_key(win, code, mods, "");
}

static void test_scroll(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 200);
    idunn_widget_t *root = idunn_column(0, 0);
    idunn_widget_t *content = idunn_column(2, 4);
    idunn_widget_t *buttons[20];
    for (int i = 0; i < 20; i++)
        buttons[i] = idunn_add(content, idunn_button("Row", IDUNN_SECONDARY, NULL, NULL));
    idunn_widget_t *view = idunn_grow(idunn_add(root, idunn_scroll_view(content)), 1);
    idunn_window_set_root(win, root);
    idunn_window_draw(win);

    // the view takes the window's height; the content is 16 + 20 * 32 + 19 * 8 tall
    CHECK_EQ(view->rect.h, 200);
    CHECK_EQ(content->rect.h, 32 + 20 * 32 + 19 * 8);

    // the wheel: 48 a step, and at the top the event goes on to the parents
    idunn_window_scroll(win, 100, 100, -1);
    CHECK_EQ(idunn_scroll_view_offset(view), 48);
    idunn_window_tick(win, 1000);
    idunn_window_draw(win);
    CHECK_EQ(content->rect.y, -48);
    idunn_window_scroll(win, 100, 100, 5);
    CHECK_EQ(idunn_scroll_view_offset(view), 0);
    idunn_window_scroll(win, 100, 100, -100);
    CHECK_EQ(idunn_scroll_view_offset(view), content->rect.h - 200);

    // a hidden widget below the view is not hit, nor drawn over the window
    idunn_window_tick(win, 2000);
    idunn_window_draw(win);
    idunn_scroll_view_scroll_to(view, 0);
    idunn_window_draw(win);
    CHECK(buttons[10]->rect.y > 200);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, 50, buttons[0]->rect.y + 4, 0);
    CHECK(win->hover == buttons[0]);

    // Tab to a button out of view scrolls it in, with room for the ring
    for (int i = 0; i < 8; i++)
        key(win, IDUNN_KEY_TAB, 0);
    CHECK(idunn_window_focus(win) == buttons[7]);
    idunn_window_tick(win, 3000);
    idunn_window_draw(win);
    CHECK(buttons[7]->rect.y + buttons[7]->rect.h <= 200 - 3);
    CHECK(buttons[7]->rect.y >= 0);

    // keys a button does not use scroll the view: Home, End, Page Down
    key(win, IDUNN_KEY_HOME, 0);
    CHECK_EQ(idunn_scroll_view_offset(view), 0);
    key(win, IDUNN_KEY_END, 0);
    CHECK_EQ(idunn_scroll_view_offset(view), content->rect.h - 200);
    key(win, IDUNN_KEY_HOME, 0);
    key(win, IDUNN_KEY_PAGEDOWN, 0);
    CHECK_EQ(idunn_scroll_view_offset(view), 200 - 48);

    // the bar: dragging the thumb to the bottom scrolls to the end
    idunn_window_tick(win, 4000);
    idunn_scroll_view_scroll_to(view, 0);
    idunn_window_draw(win);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, 296, 10, 0);
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, 296, 10, 0);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, 296, 400, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, 296, 400, 0);
    CHECK_EQ(idunn_scroll_view_offset(view), content->rect.h - 200);

    // the thumb is drawn at the right edge in the border colour, mixed
    idunn_window_draw(win);
    gfx_color_t surface = idunn_color(&ctx->theme, IDUNN_COLOR_SURFACE);
    CHECK(*gfx_pixel_at(win->buffer, 296, 196) != surface);
    CHECK(*gfx_pixel_at(win->buffer, 296, 10) == surface);

    idunn_window_free_widgets(win);
    free(win);
}

static void test_hidden(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 200);
    idunn_widget_t *col = idunn_column(2, 0);
    idunn_box_set_align(col, IDUNN_START, IDUNN_START);
    idunn_widget_t *a = idunn_add(col, idunn_button("A", IDUNN_SECONDARY, NULL, NULL));
    idunn_widget_t *b = idunn_add(col, idunn_button("B", IDUNN_SECONDARY, NULL, NULL));
    idunn_window_set_root(win, col);
    idunn_window_draw(win);
    CHECK_EQ(b->rect.y, 40);

    // a hidden widget takes no room and no Tab
    key(win, IDUNN_KEY_TAB, 0);
    CHECK(idunn_window_focus(win) == a);
    idunn_set_hidden(a, true);
    CHECK(idunn_window_focus(win) == NULL);
    idunn_window_draw(win);
    CHECK_EQ(b->rect.y, 0);
    key(win, IDUNN_KEY_TAB, 0);
    CHECK(idunn_window_focus(win) == b);
    idunn_set_hidden(a, false);
    idunn_window_draw(win);
    CHECK_EQ(b->rect.y, 40);

    // a card: the card colour inside a border, 24 px in
    idunn_widget_t *card = idunn_add(col, idunn_card(4));
    idunn_card_header(card, "Storage", "How the disk is used.");
    idunn_window_draw(win);
    CHECK_EQ(card->children[0]->rect.x, card->rect.x + 24);
    CHECK(*gfx_pixel_at(win->buffer, card->rect.x + card->rect.w / 2, card->rect.y + 2) ==
          idunn_color(&ctx->theme, IDUNN_COLOR_CARD));

    idunn_window_free_widgets(win);
    free(win);
}

void test_lists(idunn_context_t *ctx)
{
    test_scroll(ctx);
    test_hidden(ctx);
}

/**
 * @file inputs_test.c
 * @brief host tests of idunn's text editing and inputs.
 */

#include "../idunn_internal.h"
#include "hosttest.h"

#include <stdlib.h>
#include <string.h>

void test_inputs(idunn_context_t *ctx);

static int changes;
static int submits;

static void count_change(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    changes++;
}

static void count_submit(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    submits++;
}

static void key(idunn_window_t *win, uint16_t code, uint32_t mods, const char *text)
{
    idunn_window_key(win, code, mods, text);
}

static void type(idunn_window_t *win, const char *text)
{
    // one key per character (UTF-8 characters whole)
    while (*text)
    {
        char one[8] = {0};
        size_t n = 1;
        while (((unsigned char)text[n] & 0xC0) == 0x80)
            n++;
        memcpy(one, text, n);
        key(win, 0, 0, one);
        text += n;
    }
}

static void test_edit(void)
{
    idunn_edit_t e = {0};
    CHECK(idunn_edit_set(&e, "", 0));
    CHECK(idunn_edit_insert(&e, "hello world", 11));
    CHECK_EQ(e.caret, 11);
    CHECK_EQ(idunn_edit_word_left(&e, 11), 6);
    CHECK_EQ(idunn_edit_word_left(&e, 6), 0);
    CHECK_EQ(idunn_edit_word_right(&e, 0), 5);
    CHECK_EQ(idunn_edit_word_right(&e, 5), 11);

    // Ctrl+Backspace deletes a word
    idunn_edit_delete(&e, false, true);
    CHECK(strcmp(e.text, "hello ") == 0);

    // characters, not bytes: æ is two
    idunn_edit_insert(&e, "\xc3\xa6x", 3);
    CHECK_EQ(e.length, 9);
    CHECK_EQ(idunn_edit_prev(&e, 8), 6);
    CHECK_EQ(idunn_edit_next(&e, 6), 8);
    idunn_edit_move(&e, 8, false);
    idunn_edit_delete(&e, false, false);
    CHECK(strcmp(e.text, "hello x") == 0);

    // a double click on a word selects it, on spaces the spaces
    idunn_edit_select_word(&e, 2);
    CHECK_EQ(idunn_edit_start(&e), 0);
    CHECK_EQ(idunn_edit_end(&e), 5);
    idunn_edit_select_word(&e, 5);
    CHECK_EQ(idunn_edit_start(&e), 5);
    CHECK_EQ(idunn_edit_end(&e), 6);

    // typing replaces the selection; a pasted line keeps one line
    idunn_edit_insert(&e, "-", 1);
    CHECK(strcmp(e.text, "hello-x") == 0);
    idunn_edit_paste(&e, "a\r\nb\tc", 6, false);
    CHECK(strcmp(e.text, "hello-a b cx") == 0);
    idunn_edit_free(&e);
}

static void test_text_field(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 60);
    idunn_widget_t *row = idunn_row(0, 2);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
    idunn_widget_t *field = idunn_grow(idunn_add(row, idunn_text_field("Name")), 1);
    idunn_text_field_on_change(field, count_change, NULL);
    idunn_text_field_on_submit(field, count_submit, NULL);
    idunn_window_set_root(win, row);
    idunn_window_draw(win);
    CHECK_EQ(field->rect.h, 32);
    CHECK_EQ(idunn_window_cursor(win), IDUNN_CURSOR_ARROW);

    // a click focuses it, with the ring (it takes typing), and the I-beam
    gfx_rect_t r = field->rect;
    idunn_window_tick(win, 1000);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, r.x + 20, r.y + 10, 0);
    CHECK_EQ(idunn_window_cursor(win), IDUNN_CURSOR_TEXT);
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, r.x + 20, r.y + 10, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, r.x + 20, r.y + 10, 0);
    CHECK(idunn_window_focus(win) == field);
    CHECK(idunn_ring_visible(field));

    // typing, with UTF-8, and Backspace
    changes = 0;
    type(win, "Smørrebrød");
    CHECK(strcmp(idunn_text_field_text(field), "Smørrebrød") == 0);
    CHECK_EQ(changes, 10);
    key(win, IDUNN_KEY_BACKSPACE, 0, "");
    CHECK(strcmp(idunn_text_field_text(field), "Smørrebrø") == 0);

    // Shift+Left selects, typing replaces, Ctrl+A and Ctrl+C, Ctrl+V
    key(win, IDUNN_KEY_LEFT, IDUNN_MOD_SHIFT, "");
    key(win, IDUNN_KEY_LEFT, IDUNN_MOD_SHIFT, "");
    type(win, "x");
    CHECK(strcmp(idunn_text_field_text(field), "Smørrebx") == 0);
    key(win, IDUNN_KEY_A, IDUNN_MOD_CTRL, "");
    key(win, IDUNN_KEY_C, IDUNN_MOD_CTRL, "");
    key(win, IDUNN_KEY_END, 0, "");
    key(win, IDUNN_KEY_V, IDUNN_MOD_CTRL, "");
    CHECK(strcmp(idunn_text_field_text(field), "SmørrebxSmørrebx") == 0);

    // Ctrl+X takes the selection away; Home and Shift+End select all
    key(win, IDUNN_KEY_HOME, 0, "");
    key(win, IDUNN_KEY_END, IDUNN_MOD_SHIFT, "");
    key(win, IDUNN_KEY_X, IDUNN_MOD_CTRL, "");
    CHECK(strcmp(idunn_text_field_text(field), "") == 0);
    key(win, IDUNN_KEY_V, IDUNN_MOD_CTRL, "");
    CHECK(strcmp(idunn_text_field_text(field), "SmørrebxSmørrebx") == 0);

    // Enter submits; other keys go on to the parents
    submits = 0;
    key(win, IDUNN_KEY_ENTER, 0, "\n");
    CHECK_EQ(submits, 1);

    // long text scrolls so the caret stays in the field
    idunn_text_field_set_text(field, "a long line of text that does not fit in the field at all");
    key(win, IDUNN_KEY_END, 0, "");
    idunn_window_draw(win);
    // the end of the text is drawn: there is ink near the right edge
    bool ink_right = false;
    gfx_color_t well = *gfx_pixel_at(win->buffer, r.x + r.w - 16, r.y + 3);
    for (int32_t y = r.y + 8; y < r.y + r.h - 8; y++)
    {
        if (*gfx_pixel_at(win->buffer, r.x + r.w - 20, y) != well)
            ink_right = true;
    }
    CHECK(ink_right);

    // a double click selects a word, the next press puts the caret
    idunn_text_field_set_text(field, "one two");
    idunn_window_draw(win);
    int32_t x_two = r.x + 13 + 40;
    idunn_window_tick(win, 5000);
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, x_two, r.y + 10, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, x_two, r.y + 10, 0);
    idunn_window_tick(win, 5100);
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, x_two, r.y + 10, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, x_two, r.y + 10, 0);
    type(win, "2");
    CHECK(strcmp(idunn_text_field_text(field), "one 2") == 0);

    // drag selects from the press to the pointer
    idunn_window_tick(win, 9000);
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, r.x + 2, r.y + 10, 0);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, r.x + r.w - 4, r.y + 10, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, r.x + r.w - 4, r.y + 10, 0);
    type(win, "z");
    CHECK(strcmp(idunn_text_field_text(field), "z") == 0);

    // the caret blinks: a timer every 500 ms while it has the focus
    uint32_t wake = idunn_window_next_wake(win);
    CHECK(wake > 9000);
    idunn_window_draw(win);
    idunn_window_tick(win, wake);
    CHECK(!gfx_rect_empty(idunn_window_draw(win)));
    CHECK(idunn_window_next_wake(win) == wake + 500);

    // a click elsewhere takes the focus away and the blinking stops
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, 1, 1, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, 1, 1, 0);
    CHECK(idunn_window_focus(win) == NULL);
    idunn_window_tick(win, wake + 500);
    CHECK_EQ(idunn_window_next_wake(win), 0);

    // an error: the border is red even without the focus
    idunn_set_error(field, true);
    idunn_window_draw(win);
    gfx_color_t border = *gfx_pixel_at(win->buffer, r.x + r.w / 2, r.y);
    CHECK(((border >> 16) & 0xFF) > ((border >> 8) & 0xFF) + 60);

    idunn_window_free_widgets(win);
    free(win);
}

void test_inputs(idunn_context_t *ctx)
{
    test_edit();
    test_text_field(ctx);
}

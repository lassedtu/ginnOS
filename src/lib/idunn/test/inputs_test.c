/**
 * @file inputs_test.c
 * @brief host tests of idunn's text editing and inputs.
 */

#include "../idunn_internal.h"
#include "hosttest.h"

#include <stdlib.h>
#include <string.h>

void test_inputs(idunn_context_t *ctx);
void compare_reference(const char *name, const gfx_buffer_t *buf, double mean_limit);

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

static void click_at(idunn_window_t *win, int32_t x, int32_t y)
{
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, x, y, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, x, y, 0);
}

static void test_choices(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 400, 300);
    idunn_widget_t *col = idunn_column(2, 2);
    idunn_box_set_align(col, IDUNN_START, IDUNN_START);
    changes = 0;
    idunn_widget_t *check = idunn_add(col, idunn_check_box("Show hidden files", false,
                                                           count_change, NULL));
    static const char *const sizes[] = {"Small", "Medium", "Large"};
    idunn_widget_t *radio = idunn_add(col, idunn_radio_group(sizes, 3, 1, count_change, NULL));
    idunn_widget_t *sw = idunn_add(col, idunn_switch("Natural scrolling", false, count_change, NULL));
    idunn_widget_t *slider = idunn_add(col, idunn_slider(0, 10, 1, 6, count_change, NULL));
    idunn_window_set_root(win, col);
    idunn_window_draw(win);

    // a check box with its label: 16 + 10 + the text, as high as a line
    CHECK_EQ(check->rect.h, 18);
    CHECK(check->rect.w > 26 + 60);

    // a click on the label checks it; Space unchecks; mixed goes to checked
    click_at(win, check->rect.x + check->rect.w - 4, check->rect.y + 9);
    CHECK_EQ(idunn_check_box_state(check), IDUNN_CHECKED);
    key(win, IDUNN_KEY_SPACE, 0, " ");
    CHECK_EQ(idunn_check_box_state(check), IDUNN_UNCHECKED);
    idunn_check_box_set_state(check, IDUNN_MIXED);
    click_at(win, check->rect.x + 8, check->rect.y + 9);
    CHECK_EQ(idunn_check_box_state(check), IDUNN_CHECKED);
    CHECK_EQ(changes, 3);

    // the radio group: three choices 8 apart; a press chooses, the arrows move
    CHECK_EQ(radio->rect.h, 3 * 18 + 2 * 8);
    click_at(win, radio->rect.x + 8, radio->rect.y + 2 * 26 + 9);
    CHECK_EQ(idunn_radio_group_chosen(radio), 2);
    key(win, IDUNN_KEY_DOWN, 0, "");
    CHECK_EQ(idunn_radio_group_chosen(radio), 0);
    key(win, IDUNN_KEY_UP, 0, "");
    CHECK_EQ(idunn_radio_group_chosen(radio), 2);
    idunn_radio_group_set_row(radio, true);
    idunn_window_draw(win);
    CHECK_EQ(radio->rect.h, 18);

    // one Tab stop for the group: check box, radio, switch, slider
    key(win, IDUNN_KEY_TAB, 0, "");
    CHECK(idunn_window_focus(win) == sw);
    key(win, IDUNN_KEY_TAB, IDUNN_MOD_SHIFT, "");
    CHECK(idunn_window_focus(win) == radio);

    // the switch slides in 120 ms
    idunn_window_tick(win, 2000);
    click_at(win, sw->rect.x + 4, sw->rect.y + 9);
    CHECK(idunn_switch_on(sw));
    idunn_window_draw(win);
    idunn_window_tick(win, 2060);
    idunn_window_draw(win);
    CHECK(idunn_window_tick(win, 2060));
    idunn_window_tick(win, 2120);
    idunn_window_draw(win);
    CHECK(!idunn_window_tick(win, 2120));
    key(win, IDUNN_KEY_SPACE, 0, " ");
    CHECK(!idunn_switch_on(sw));

    // the slider: a press at its end, a drag back, the keys
    gfx_rect_t r = slider->rect;
    CHECK_EQ(r.h, 16);
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, r.x + r.w - 1, r.y + 8, 0);
    CHECK_EQ(idunn_slider_value(slider), 10);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, r.x + 8 + (r.w - 16) * 3 / 10, r.y + 8, 0);
    CHECK_EQ(idunn_slider_value(slider), 3);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, r.x - 50, r.y + 8, 0);
    CHECK_EQ(idunn_slider_value(slider), 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, r.x - 50, r.y + 8, 0);
    key(win, IDUNN_KEY_RIGHT, 0, "");
    CHECK_EQ(idunn_slider_value(slider), 1);
    key(win, IDUNN_KEY_PAGEUP, 0, "");
    CHECK_EQ(idunn_slider_value(slider), 10);
    key(win, IDUNN_KEY_HOME, 0, "");
    CHECK_EQ(idunn_slider_value(slider), 0);

    // a pointer event after a change of layout does not draw: the frame
    // it would draw (and its damage) is for the app loop to show
    idunn_widget_t *value = idunn_add(col, idunn_label("6", IDUNN_TEXT_LABEL));
    idunn_window_tick(win, 10000); // the transitions are over
    idunn_window_draw(win);
    CHECK(gfx_rect_empty(idunn_window_draw(win)));
    idunn_label_set_text(value, "10 of 10");
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, r.x - 50, r.y + 8, 0); // no new hover
    CHECK(!gfx_rect_empty(idunn_window_draw(win)));

    idunn_window_free_widgets(win);
    free(win);
}

static void test_field(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 200);
    idunn_widget_t *col = idunn_column(0, 0);
    idunn_widget_t *input = idunn_text_field("you@example.com");
    idunn_widget_t *field = idunn_add(col, idunn_field("Email", input, "We never share it."));
    idunn_window_set_root(win, col);
    idunn_window_draw(win);

    // the label (18), 6, the control (32), 6, the help (16)
    CHECK_EQ(field->rect.h, 18 + 6 + 32 + 6 + 16);
    CHECK_EQ(input->rect.y, 24);
    CHECK_EQ(input->rect.w, 300);

    idunn_field_set_error(field, "That is not an email address.");
    CHECK(input->state & IDUNN_ERROR);
    idunn_field_set_error(field, NULL);
    CHECK(!(input->state & IDUNN_ERROR));
    CHECK_EQ(field->child_count, 3);

    // without help text, the error line comes and goes
    idunn_widget_t *bare = idunn_add(col, idunn_field("Name", idunn_text_field(NULL), NULL));
    CHECK_EQ(bare->child_count, 2);
    idunn_field_set_error(bare, "Required.");
    CHECK_EQ(bare->child_count, 3);
    idunn_field_set_error(bare, NULL);
    CHECK_EQ(bare->child_count, 2);

    idunn_window_free_widgets(win);
    free(win);
}

static void test_text_area(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 200);
    idunn_widget_t *col = idunn_column(0, 2);
    idunn_box_set_align(col, IDUNN_START, IDUNN_START);
    idunn_widget_t *area = idunn_add(col, idunn_text_area("Notes", 3));
    idunn_window_set_root(win, col);
    idunn_window_draw(win);

    // 3 lines of 18, 8 padding and a 1 px border above and below
    CHECK_EQ(area->rect.h, 3 * 18 + 2 * 9);
    click_at(win, area->rect.x + 20, area->rect.y + 20);
    CHECK(idunn_window_focus(win) == area);

    // Enter makes lines; Up keeps the column; Home and End go along the line
    type(win, "first");
    key(win, IDUNN_KEY_ENTER, 0, "\n");
    type(win, "second line");
    CHECK(strcmp(idunn_text_area_text(area), "first\nsecond line") == 0);
    key(win, IDUNN_KEY_UP, 0, "");
    type(win, "!");
    CHECK(strcmp(idunn_text_area_text(area), "first!\nsecond line") == 0);
    key(win, IDUNN_KEY_DOWN, 0, "");
    key(win, IDUNN_KEY_HOME, 0, "");
    type(win, ">");
    CHECK(strcmp(idunn_text_area_text(area), "first!\n>second line") == 0);
    key(win, IDUNN_KEY_END, IDUNN_MOD_SHIFT, "");
    key(win, IDUNN_KEY_BACKSPACE, 0, "");
    CHECK(strcmp(idunn_text_area_text(area), "first!\n>") == 0);

    // a long line wraps: Down goes to the next part of it, not past it
    idunn_text_area_set_text(area, "one two three four five six seven eight nine ten eleven twelve");
    key(win, IDUNN_KEY_HOME, IDUNN_MOD_CTRL, "");
    key(win, IDUNN_KEY_DOWN, 0, "");
    type(win, "|");
    const char *text = idunn_text_area_text(area);
    const char *bar = strchr(text, '|');
    CHECK(bar != NULL && bar > text + 10 && bar < text + 50);

    // many lines: the caret at the end scrolls, Ctrl+Home scrolls back
    idunn_text_area_set_text(area, "1\n2\n3\n4\n5\n6\n7\n8");
    key(win, IDUNN_KEY_END, IDUNN_MOD_CTRL, "");
    idunn_window_draw(win);
    key(win, IDUNN_KEY_HOME, IDUNN_MOD_CTRL, "");
    type(win, "0");
    CHECK(strncmp(idunn_text_area_text(area), "01\n2", 4) == 0);

    // the wheel scrolls, and at the end leaves the event to the parents
    idunn_window_scroll(win, area->rect.x + 10, area->rect.y + 10, -1);
    idunn_window_scroll(win, area->rect.x + 10, area->rect.y + 10, -5);
    idunn_window_scroll(win, area->rect.x + 10, area->rect.y + 10, 9);

    // a press on the third shown line puts the caret there
    click_at(win, area->rect.x + 13, area->rect.y + 9 + 2 * 18 + 9);
    type(win, "x");
    CHECK(strncmp(idunn_text_area_text(area), "01\n2\nx3", 7) == 0);

    idunn_window_free_widgets(win);
    free(win);
}

/**
 * a text field, check boxes, a radio and switches in one mode, compared
 * with the preview's CSS drawn by Chrome.
 */
static void reference_inputs(idunn_context_t *ctx, const char *name, idunn_mode_t mode,
                             double mean_limit)
{
    idunn_theme_t theme = ctx->theme;
    theme.mode = mode;
    idunn_context_set_theme(ctx, &theme);
    idunn_window_t *win = idunn_window_offscreen(ctx, 400, 64);
    idunn_widget_t *row = idunn_row(4, 4);
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
    idunn_widget_t *field = idunn_add(row, idunn_text_field("Name"));
    idunn_set_size(field, 180, 0);
    idunn_add(row, idunn_check_box(NULL, true, NULL, NULL));
    idunn_add(row, idunn_check_box(NULL, false, NULL, NULL));
    static const char *const one[] = {""};
    idunn_add(row, idunn_radio_group(one, 1, 0, NULL, NULL));
    idunn_add(row, idunn_switch(NULL, true, NULL, NULL));
    idunn_add(row, idunn_switch(NULL, false, NULL, NULL));
    idunn_window_set_root(win, row);
    idunn_window_draw(win);
    compare_reference(name, win->buffer, mean_limit);
    idunn_window_free_widgets(win);
    free(win);
    theme.mode = IDUNN_DARK;
    idunn_context_set_theme(ctx, &theme);
}

void test_inputs(idunn_context_t *ctx)
{
    test_edit();
    test_text_field(ctx);
    test_choices(ctx);
    test_field(ctx);
    test_text_area(ctx);
    reference_inputs(ctx, "inputs-dark", IDUNN_DARK, 1.5);
    reference_inputs(ctx, "inputs-light", IDUNN_LIGHT, 1.5);
}

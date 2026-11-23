/**
 * @file buffer_test.c
 * @brief host tests of Saga's text buffer: editing, line ends, undo and
 *        redo, the modified mark, moving and finding.
 */

#include "../buffer.h"
#include "hosttest.h"

#include <stdlib.h>
#include <string.h>

static bool text_is(const buffer_t *b, const char *want)
{
    size_t n;
    char *got = buffer_text(b, &n);
    bool same = got && n == strlen(want) && memcmp(got, want, n) == 0;
    free(got);
    return same;
}

static void type(buffer_t *b, pos_t *at, const char *text)
{
    for (const char *c = text; *c; c++)
        *at = buffer_insert(b, *at, c, 1, true);
}

static void test_edit(void)
{
    buffer_t b;
    buffer_init(&b);
    CHECK(text_is(&b, ""));
    CHECK_EQ(b.count, 1);

    // a text with a newline at its end keeps it
    buffer_set_text(&b, "one\ntwo\n", 8);
    CHECK_EQ(b.count, 3);
    CHECK(text_is(&b, "one\ntwo\n"));
    CHECK(!buffer_modified(&b));

    // insert across lines, then delete across lines
    pos_t end = buffer_insert(&b, (pos_t){0, 2}, "X\nY", 3, false);
    CHECK(text_is(&b, "onX\nYe\ntwo\n"));
    CHECK_EQ(end.line, 1);
    CHECK_EQ(end.col, 1);
    CHECK(buffer_modified(&b));
    buffer_delete(&b, (pos_t){1, 1}, (pos_t){0, 2});
    CHECK(text_is(&b, "one\ntwo\n"));

    // a range with LF ends
    size_t n;
    char *r = buffer_range(&b, (pos_t){0, 1}, (pos_t){1, 2}, &n);
    CHECK(r && strcmp(r, "ne\ntw") == 0);
    free(r);

    // CR LF is read as LF and written back as CR LF
    buffer_set_text(&b, "a\r\nb", 4);
    CHECK_EQ(b.count, 2);
    CHECK_EQ(b.lines[0].length, 1);
    CHECK(text_is(&b, "a\r\nb"));
    buffer_free(&b);
}

static void test_undo(void)
{
    buffer_t b;
    buffer_init(&b);
    pos_t at = {0, 0};
    type(&b, &at, "hello world");
    CHECK(text_is(&b, "hello world"));

    // typing undoes a word at a time
    pos_t caret;
    CHECK(buffer_undo(&b, &caret));
    CHECK(text_is(&b, "hello "));
    CHECK_EQ(caret.col, 6);
    CHECK(buffer_undo(&b, &caret));
    CHECK(text_is(&b, ""));
    CHECK(!buffer_undo(&b, &caret));
    CHECK(!buffer_modified(&b));

    // redo brings it back; a new edit ends the redo history
    CHECK(buffer_redo(&b, &caret));
    CHECK(text_is(&b, "hello "));
    CHECK(buffer_modified(&b));
    buffer_insert(&b, caret, "there", 5, false);
    CHECK(!buffer_redo(&b, &caret));
    CHECK(text_is(&b, "hello there"));

    // the saved mark: undo back to it is not modified; past it is
    buffer_mark_saved(&b);
    CHECK(!buffer_modified(&b));
    buffer_delete(&b, (pos_t){0, 0}, (pos_t){0, 6});
    CHECK(text_is(&b, "there"));
    CHECK(buffer_modified(&b));
    buffer_undo(&b, &caret);
    CHECK(!buffer_modified(&b));
    buffer_undo(&b, &caret);
    CHECK(buffer_modified(&b));
    // a new edit here: the saved text cannot come back
    buffer_insert(&b, (pos_t){0, 0}, "x", 1, false);
    buffer_undo(&b, &caret);
    CHECK(buffer_modified(&b));

    // steps joined into one undo together
    buffer_set_text(&b, "a\nb", 3);
    size_t mark = b.undo.count;
    buffer_insert(&b, (pos_t){0, 0}, "  ", 2, false);
    buffer_insert(&b, (pos_t){1, 0}, "  ", 2, false);
    buffer_join_steps(&b, mark);
    CHECK(text_is(&b, "  a\n  b"));
    buffer_undo(&b, &caret);
    CHECK(text_is(&b, "a\nb"));

    // undo of a delete across lines
    buffer_set_text(&b, "ab\ncd", 5);
    buffer_delete(&b, (pos_t){0, 1}, (pos_t){1, 1});
    CHECK(text_is(&b, "ad"));
    buffer_undo(&b, &caret);
    CHECK(text_is(&b, "ab\ncd"));
    // the caret goes after the text that came back
    CHECK_EQ(caret.line, 1);
    CHECK_EQ(caret.col, 1);
    buffer_free(&b);
}

static void test_moves(void)
{
    buffer_t b;
    buffer_init(&b);
    // "ø" is two bytes: the caret steps over both
    buffer_set_text(&b, "sø ord\nx", 9);
    pos_t p = {0, 1};
    p = buffer_next(&b, p);
    CHECK_EQ(p.col, 3);
    p = buffer_prev(&b, p);
    CHECK_EQ(p.col, 1);
    p = buffer_clamp(&b, (pos_t){0, 2});
    CHECK_EQ(p.col, 1);
    // across a line end
    p = buffer_next(&b, (pos_t){0, 7});
    CHECK_EQ(p.line, 1);
    CHECK_EQ(p.col, 0);
    p = buffer_prev(&b, p);
    CHECK_EQ(p.line, 0);
    // words
    p = buffer_word_end(&b, (pos_t){0, 0});
    CHECK_EQ(p.col, 3);
    p = buffer_word_end(&b, p);
    CHECK_EQ(p.col, 7);
    p = buffer_word_start(&b, p);
    CHECK_EQ(p.col, 4);
    buffer_free(&b);
}

static void test_find(void)
{
    buffer_t b;
    buffer_init(&b);
    buffer_set_text(&b, "Odin and Thor\nthor again\nodin", 29);
    pos_t s, e;
    CHECK(buffer_find(&b, "thor", false, (pos_t){0, 0}, false, &s, &e));
    CHECK_EQ(s.line, 0);
    CHECK_EQ(s.col, 9);
    CHECK(buffer_find(&b, "thor", false, e, false, &s, &e));
    CHECK_EQ(s.line, 1);
    // case matters when asked
    CHECK(buffer_find(&b, "Thor", true, (pos_t){1, 0}, false, &s, &e));
    CHECK_EQ(s.line, 0); // round from the end
    // backward from the end finds the last one
    CHECK(buffer_find(&b, "odin", false, buffer_end(&b), true, &s, &e));
    CHECK_EQ(s.line, 2);
    CHECK(!buffer_find(&b, "loki", false, (pos_t){0, 0}, false, &s, &e));
    buffer_free(&b);
}

int main(void)
{
    test_edit();
    test_undo();
    test_moves();
    test_find();
    return hosttest_done("saga buffer");
}

#pragma once

/**
 * @file buffer.h
 * @brief Saga: the text, as an array of lines, with undo and redo.
 *
 * plain C (no idunn), so the host tests run it. a position is a line and
 * a byte offset in it; positions are always at the start of a UTF-8
 * character. the text "a\nb\n" is the lines "a", "b" and "": a file that
 * ends with a newline ends with an empty line, so it is written back the
 * same. CR LF line ends are read as LF and written as they were read.
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    size_t line;
    size_t col; // bytes from the start of the line
} pos_t;

typedef struct
{
    char *text;
    size_t length;
    size_t capacity;
} line_t;

typedef struct
{
    bool insert; // else a delete
    pos_t at;
    char *text;
    size_t length;
    unsigned group; // undo takes back all the steps of a group at once
} step_t;

typedef struct
{
    step_t *steps;
    size_t count;
    size_t capacity;
} steps_t;

typedef struct
{
    line_t *lines;
    size_t count; // at least 1
    size_t capacity;
    bool crlf;    // the file had CR LF line ends

    steps_t undo;
    steps_t redo;
    unsigned group;    // the group of the next step
    bool typing;       // the last step was typing: more typing joins it
    size_t saved_at;   // undo.count when the text was saved (or read)
    bool saved_valid;  // false when that state cannot come back by undo
} buffer_t;

bool buffer_init(buffer_t *b);
void buffer_free(buffer_t *b);

/** the whole text, new; the undo history is empty and nothing is modified */
bool buffer_set_text(buffer_t *b, const char *text, size_t length);

/** the whole text (malloc'd, zero-terminated), with the line ends it had */
char *buffer_text(const buffer_t *b, size_t *length);

/** the text from one position to another (malloc'd), with LF line ends */
char *buffer_range(const buffer_t *b, pos_t from, pos_t to, size_t *length);

/**
 * insert text (LF or CR LF line ends) at a position.
 * @param typing true for one typed character: it joins the typing before
 *        it into one undo step.
 * @return the position after the text.
 */
pos_t buffer_insert(buffer_t *b, pos_t at, const char *text, size_t length, bool typing);

/** delete the text between two positions (in any order) */
void buffer_delete(buffer_t *b, pos_t from, pos_t to);

/** the next edit starts a new undo step */
void buffer_break_group(buffer_t *b);

/** the undo steps from mark (an undo.count) on are one step: undo takes them back together */
void buffer_join_steps(buffer_t *b, size_t mark);

/**
 * take back the last undo step (or do it again).
 * @param caret receives where the caret goes.
 * @return false when there is nothing to undo (redo).
 */
bool buffer_undo(buffer_t *b, pos_t *caret);
bool buffer_redo(buffer_t *b, pos_t *caret);

bool buffer_modified(const buffer_t *b);
/** the text is saved as it is now */
void buffer_mark_saved(buffer_t *b);

/** position order: negative, 0 or positive */
int pos_compare(pos_t a, pos_t b);
/** a position moved to the nearest valid one */
pos_t buffer_clamp(const buffer_t *b, pos_t p);
/** the next and the previous character (across line ends) */
pos_t buffer_next(const buffer_t *b, pos_t p);
pos_t buffer_prev(const buffer_t *b, pos_t p);
/** the end of the word at or after p, and the start of the word before p */
pos_t buffer_word_end(const buffer_t *b, pos_t p);
pos_t buffer_word_start(const buffer_t *b, pos_t p);
/** the end of the text */
pos_t buffer_end(const buffer_t *b);

/**
 * find text from a position (forward, wrapping at the end once).
 * @return true with the match's start and end.
 */
bool buffer_find(const buffer_t *b, const char *text, bool match_case, pos_t from, bool backward,
                 pos_t *start, pos_t *end);

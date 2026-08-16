/**
 * @file line.c
 * @brief Line editing for shell input.
 *
 * This file implements a simple line editor for the shell, providing basic editing capabilities similar to readline. It supports cursor movement, character insertion and deletion, and line submission.
 */

// Provides a readline-like editing experience:
// - Left/Right arrow: move cursor
// - Home/End: jump to start/end
// - Backspace: delete char before cursor
// - Delete: delete char at cursor
// - Ctrl+A: home
// - Ctrl+E: end
// - Ctrl+K: kill to end of line
// - Ctrl+U: kill to start of line
// - Ctrl+W: kill previous word
// - Ctrl+L: clear screen, reprint prompt+line
// - Ctrl+D: EOF (if line is empty)
// - Enter: submit line

#include "line.h"
#include "history.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

// forward declarations
static void emit(const char *s, int len);
static void emit_char(char c);

/**
 * emit the ANSI "erase to end of line" sequence.
 */
static void emit_erase_eol(void)
{
    char seq[3];
    seq[0] = 27;
    seq[1] = '[';
    seq[2] = 'K';
    emit(seq, 3);
}

/**
 * emit the ANSI "clear screen + cursor home" sequence.
 */
static void emit_clear_home(void)
{
    char seq[7];
    seq[0] = 27;
    seq[1] = '[';
    seq[2] = '2';
    seq[3] = 'J';
    seq[4] = 27;
    seq[5] = '[';
    seq[6] = 'H';
    emit(seq, 7);
}
static char line_buf[LINE_MAX];
static int line_len; // number of characters in the buffer
static int line_pos; // cursor position within the buffer

/**
 * write raw bytes to stdout (no printf formatting overhead).
 */
static void emit(const char *s, int len)
{
    write(1, s, len);
}

/**
 * emit a single character.
 */
static void emit_char(char c)
{
    write(1, &c, 1);
}

/**
 * move the terminal cursor to a specific position on the current line.
 * uses carriage return + forward movement for reliability.
 */
static void move_cursor_to(int prompt_len, int pos)
{
    // carriage return to column 0
    char cr = '\r';
    emit(&cr, 1);

    // move forward prompt_len + pos columns using CSI C (cursor forward)
    int total = prompt_len + pos;
    if (total > 0)
    {
        char seq[16];
        int i = 0;
        seq[i++] = 27;
        seq[i++] = '[';
        if (total >= 100)
        {
            seq[i++] = (char)('0' + total / 100);
            seq[i++] = (char)('0' + (total / 10) % 10);
            seq[i++] = (char)('0' + total % 10);
        }
        else if (total >= 10)
        {
            seq[i++] = (char)('0' + total / 10);
            seq[i++] = (char)('0' + total % 10);
        }
        else
        {
            seq[i++] = (char)('0' + total);
        }
        seq[i++] = 'C';
        emit(seq, i);
    }
}

/**
 * refresh the line display: redraws the full line content and
 * positions the cursor correctly.
 */
static void refresh_line(int prompt_len)
{
    // go to start of line content
    char cr = '\r';
    emit(&cr, 1);

    // re-emit the prompt (we need it to be there for the cursor to be right)
    extern void print_prompt(void);
    print_prompt();

    // print the full line buffer
    if (line_len > 0)
        emit(line_buf, line_len);

    // erase any leftover characters
    emit_erase_eol();

    // move cursor to the correct position
    move_cursor_to(prompt_len, line_pos);
}

/**
 * redraw the entire line (same as refresh_line).
 */
static void redraw_full(int prompt_len)
{
    refresh_line(prompt_len);
}

/**
 * replace the entire line buffer with new content and redraw.
 */
static void line_replace(const char *new_content, int prompt_len)
{
    int len = strlen(new_content);
    if (len >= LINE_MAX)
        len = LINE_MAX - 1;

    memcpy(line_buf, new_content, len);
    line_buf[len] = '\0';
    line_len = len;
    line_pos = len;
    redraw_full(prompt_len);
}

// store prompt length for refresh operations
static int current_prompt_len;

void line_init(void)
{
    ttyctl(TTY_RAW);
}

int line_read(char *buf, int size)
{
    key_event_t event;

    line_len = 0;
    line_pos = 0;
    line_buf[0] = '\0';

    // calculate prompt length by reading cursor column
    // the prompt has already been printed, so current column = prompt length
    // we'll approximate based on the shell's prompt format "skl:<cwd> $ "
    // actually, we'll get it from the caller via a global
    int prompt_len = current_prompt_len;

    while (1)
    {
        if (read_event(&event) < 0)
            continue;

        if (event.type == KEY_EVENT_CHAR)
        {
            char c = event.character;

            if (c == '\n')
            {
                // submit line
                emit_char('\r');
                emit_char('\n');
                history_reset_nav();
                int len = line_len < size - 1 ? line_len : size - 1;
                memcpy(buf, line_buf, len);
                buf[len] = '\0';
                return len;
            }

            if (c == 4) // Ctrl+D
            {
                if (line_len == 0)
                    return -1; // EOF
                // with content, Ctrl+D does nothing (or delete-at-cursor in some shells)
                continue;
            }

            if (c == 1) // Ctrl+A — home
            {
                line_pos = 0;
                move_cursor_to(prompt_len, line_pos);
                continue;
            }

            if (c == 5) // Ctrl+E — end
            {
                line_pos = line_len;
                move_cursor_to(prompt_len, line_pos);
                continue;
            }

            if (c == 11) // Ctrl+K — kill to end
            {
                line_len = line_pos;
                line_buf[line_len] = '\0';
                emit_erase_eol(); // erase to end of line
                continue;
            }

            if (c == 21) // Ctrl+U — kill to start
            {
                int removed = line_pos;
                if (removed > 0)
                {
                    // shift buffer left
                    for (int i = 0; i < line_len - line_pos; i++)
                        line_buf[i] = line_buf[line_pos + i];
                    line_len -= removed;
                    line_pos = 0;
                    line_buf[line_len] = '\0';
                    redraw_full(prompt_len);
                }
                continue;
            }

            if (c == 23) // Ctrl+W — kill previous word
            {
                if (line_pos > 0)
                {
                    int old_pos = line_pos;
                    // skip trailing spaces
                    while (line_pos > 0 && line_buf[line_pos - 1] == ' ')
                        line_pos--;
                    // skip word characters
                    while (line_pos > 0 && line_buf[line_pos - 1] != ' ')
                        line_pos--;

                    int removed = old_pos - line_pos;
                    // shift buffer
                    for (int i = line_pos; i < line_len - removed; i++)
                        line_buf[i] = line_buf[i + removed];
                    line_len -= removed;
                    line_buf[line_len] = '\0';
                    redraw_full(prompt_len);
                }
                continue;
            }

            if (c == 12) // Ctrl+L — clear screen and redraw
            {
                emit_clear_home();
                // reprint prompt (we can't call print_prompt from here,
                // so we'll emit it manually)
                // the shell will set current_prompt_len before calling us
                // for now, just print a generic prompt indicator
                // actually, let's get the prompt from main... we'll use a callback later
                // for now: just redraw from current state
                // move to row 1, col 1 and reprint prompt + line
                extern void print_prompt(void);
                print_prompt();
                redraw_full(prompt_len);
                continue;
            }

            if (c == '\b') // Backspace
            {
                if (line_pos > 0)
                {
                    // shift characters left
                    for (int i = line_pos - 1; i < line_len - 1; i++)
                        line_buf[i] = line_buf[i + 1];
                    line_pos--;
                    line_len--;
                    line_buf[line_len] = '\0';
                    refresh_line(prompt_len);
                }
                continue;
            }

            // regular printable character — insert at cursor
            if (c >= 32 && line_len < LINE_MAX - 1)
            {
                // shift characters right
                for (int i = line_len; i > line_pos; i--)
                    line_buf[i] = line_buf[i - 1];
                line_buf[line_pos] = c;
                line_pos++;
                line_len++;
                line_buf[line_len] = '\0';

                refresh_line(prompt_len);
            }
        }
        else if (event.type == KEY_EVENT_SPECIAL)
        {
            switch (event.special)
            {
            case KEY_ARROW_LEFT:
                if (line_pos > 0)
                {
                    line_pos--;
                    move_cursor_to(prompt_len, line_pos);
                }
                break;

            case KEY_ARROW_RIGHT:
                if (line_pos < line_len)
                {
                    line_pos++;
                    move_cursor_to(prompt_len, line_pos);
                }
                break;

            case KEY_HOME:
                line_pos = 0;
                move_cursor_to(prompt_len, line_pos);
                break;

            case KEY_END:
                line_pos = line_len;
                move_cursor_to(prompt_len, line_pos);
                break;

            case KEY_DELETE:
                if (line_pos < line_len)
                {
                    for (int i = line_pos; i < line_len - 1; i++)
                        line_buf[i] = line_buf[i + 1];
                    line_len--;
                    line_buf[line_len] = '\0';
                    refresh_line(prompt_len);
                }
                break;

            case KEY_ARROW_UP:
            {
                const char *entry = history_prev(line_buf);
                if (entry)
                    line_replace(entry, prompt_len);
                break;
            }

            case KEY_ARROW_DOWN:
            {
                const char *entry = history_next();
                if (entry)
                    line_replace(entry, prompt_len);
                break;
            }

            default:
                break;
            }
        }
    }
}

void line_set_prompt_len(int len)
{
    current_prompt_len = len;
}

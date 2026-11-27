/**
 * @file line.c
 * @brief Line editing for shell input.
 *
 * the terminal is in raw mode (no echo, no line editing, no signal keys)
 * while a line is edited. the editor reads bytes and decodes the escape
 * sequences a VT terminal sends for the special keys, so it works the
 * same on the console and on a pseudo-terminal.
 *
 * keys:
 * - Left/Right arrow: move cursor
 * - Up/Down arrow: history
 * - Home/End, Ctrl+A/Ctrl+E: jump to start/end
 * - Backspace: delete char before cursor
 * - Delete: delete char at cursor
 * - Ctrl+K: kill to end of line
 * - Ctrl+U: kill to start of line
 * - Ctrl+W: kill previous word
 * - Ctrl+L: clear screen, reprint prompt+line
 * - Ctrl+C: drop the line, new prompt
 * - Ctrl+D: EOF (if line is empty)
 * - Enter: submit line
 *
 * when the input is not a terminal (a script on a pipe), lines are read
 * as they are, without editing.
 *
 * a line can be longer than the terminal is wide: it then fills more than
 * one row. each change draws the prompt and the whole line again from the
 * prompt's first row (cursor_row tells how far up that is).
 */

#include "line.h"
#include "history.h"

#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// forward declarations
static void emit(const char *s, int len);
static void emit_char(char c);

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

// pending terminal output. everything the editor draws for one keystroke is
// collected here and sent with a single write() before waiting for the next
// key, so the console shows each update in one go instead of every in-between
// step (e.g. the cursor visiting column 0 while the line is redrawn).
static char out_buf[1024];
static int out_len;

/**
 * send any pending editor output to the terminal.
 */
static void flush_output(void)
{
    if (out_len > 0)
    {
        write(1, out_buf, out_len);
        out_len = 0;
    }
}

/**
 * queue raw bytes for the terminal (no printf formatting overhead).
 */
static void emit(const char *s, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (out_len == (int)sizeof(out_buf))
        {
            flush_output();
        }
        out_buf[out_len++] = s[i];
    }
}

/**
 * queue a single character.
 */
static void emit_char(char c)
{
    emit(&c, 1);
}

// the row of the terminal cursor, counted from the first row of the prompt
static int cursor_row;

/**
 * queue "ESC [ n final", a CSI sequence with one number.
 */
static void emit_csi(int n, char final)
{
    char seq[16];
    int length = snprintf(seq, sizeof(seq), "\033[%d%c", n, final);
    emit(seq, length);
}

/**
 * the width of the terminal in columns (80 if it does not tell).
 */
static int terminal_width(void)
{
    struct winsize size;
    if (ioctl(1, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
    {
        return size.ws_col;
    }
    return 80;
}

/**
 * draw the prompt and the line again, and put the cursor at line_pos.
 */
static void refresh_line(int prompt_len)
{
    int cols = terminal_width();

    // to the start of the prompt's first row, and erase from there down
    if (cursor_row > 0)
    {
        emit_csi(cursor_row, 'A');
    }
    emit_char('\r');
    emit_csi(0, 'J');

    // the prompt is formatted into our buffer, so it goes out in the same
    // single write as the rest of the line
    char prompt[PROMPT_MAX];
    int plen = format_prompt(prompt, sizeof(prompt));
    if (plen > 0)
    {
        emit(prompt, plen);
    }
    emit(line_buf, line_len);

    // text that ends at the right edge: a terminal that wraps late keeps the
    // cursor in the last column, one that wraps at once is on the next row.
    // a space and CR put both at the start of the next row.
    int end = prompt_len + line_len;
    if (end > 0 && end % cols == 0)
    {
        emit(" \r", 2);
        emit_csi(0, 'K');
    }

    // from the end of the line to the cursor position
    int end_row = end / cols;
    int target = prompt_len + line_pos;
    int target_row = target / cols;
    if (end_row > target_row)
    {
        emit_csi(end_row - target_row, 'A');
    }
    if (target != end)
    {
        emit_char('\r');
        if (target % cols > 0)
        {
            emit_csi(target % cols, 'C');
        }
    }
    cursor_row = target_row;
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
    // nothing to set up: line_read switches the terminal mode itself
}

// the special keys, as read_key returns them (above the byte values)
enum
{
    KEY_NONE = 256, // a sequence the editor does not use
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
};

#define ESC 27
#define CTRL(c) ((c) & 0x1f)

// how long to wait for the rest of an escape sequence after ESC
#define ESC_TIMEOUT_MS 50

/**
 * read one byte from stdin.
 * @param timeout_ms -1 to wait, else the longest wait.
 * @return the byte, or -1 at end of input, on an error or at the timeout.
 */
static int read_byte(int timeout_ms)
{
    if (timeout_ms >= 0)
    {
        struct pollfd pfd = {0, POLLIN, 0};
        if (poll(&pfd, 1, timeout_ms) != 1)
            return -1;
    }
    unsigned char c;
    return read(0, &c, 1) == 1 ? c : -1;
}

/**
 * the key of a CSI sequence ("ESC [" params final), from its final byte
 * and first parameter.
 */
static int csi_key(int final, int param)
{
    switch (final)
    {
    case 'A':
        return KEY_UP;
    case 'B':
        return KEY_DOWN;
    case 'C':
        return KEY_RIGHT;
    case 'D':
        return KEY_LEFT;
    case 'H':
        return KEY_HOME;
    case 'F':
        return KEY_END;
    case '~':
        switch (param)
        {
        case 1:
        case 7:
            return KEY_HOME;
        case 4:
        case 8:
            return KEY_END;
        case 3:
            return KEY_DELETE;
        default:
            return KEY_NONE;
        }
    default:
        return KEY_NONE;
    }
}

/**
 * read one key: a byte (0 to 255) or a KEY_ value for an escape sequence.
 * @return the key, or -1 at end of input or on an error.
 */
static int read_key(void)
{
    int c = read_byte(-1);
    if (c != ESC)
        return c;

    // ESC alone (the Escape key) is not followed by more bytes at once
    int kind = read_byte(ESC_TIMEOUT_MS);
    if (kind != '[' && kind != 'O')
        return KEY_NONE;

    // the parameters (digits and ';'), then the final byte
    int param = 0;
    bool first = true;
    for (;;)
    {
        int b = read_byte(ESC_TIMEOUT_MS);
        if (b < 0)
            return KEY_NONE;
        if (b >= '0' && b <= '9')
        {
            if (first)
                param = param * 10 + (b - '0');
            continue;
        }
        if (b == ';')
        {
            first = false;
            continue;
        }
        // "ESC O" has no parameters: its byte is the final one
        return csi_key(b, param);
    }
}

/**
 * delete the character before the cursor.
 */
static void delete_before(int prompt_len)
{
    if (line_pos == 0)
        return;
    for (int i = line_pos - 1; i < line_len - 1; i++)
        line_buf[i] = line_buf[i + 1];
    line_pos--;
    line_len--;
    line_buf[line_len] = '\0';
    refresh_line(prompt_len);
}

/**
 * delete the character at the cursor.
 */
static void delete_at(int prompt_len)
{
    if (line_pos >= line_len)
        return;
    for (int i = line_pos; i < line_len - 1; i++)
        line_buf[i] = line_buf[i + 1];
    line_len--;
    line_buf[line_len] = '\0';
    refresh_line(prompt_len);
}

/**
 * read and edit one line, with the terminal in raw mode.
 * @return the length, or -1 at Ctrl+D on an empty line or at end of input.
 */
static int read_line_raw(char *buf, int size)
{
    line_len = 0;
    line_pos = 0;
    line_buf[0] = '\0';
    cursor_row = 0;
    int prompt_len = current_prompt_len;

    while (1)
    {
        // show everything drawn for the previous key before blocking.
        flush_output();

        int key = read_key();
        if (key < 0)
            return -1; // end of input: the terminal is gone

        switch (key)
        {
        case '\r':
        case '\n':
        {
            // the output starts below the whole line
            line_pos = line_len;
            refresh_line(prompt_len);
            emit_char('\r');
            emit_char('\n');
            flush_output();
            history_reset_nav();
            int len = line_len < size - 1 ? line_len : size - 1;
            memcpy(buf, line_buf, len);
            buf[len] = '\0';
            return len;
        }

        case CTRL('c'):
            // drop the line; the shell prints a new prompt
            line_pos = line_len;
            refresh_line(prompt_len);
            emit("^C\r\n", 4);
            flush_output();
            history_reset_nav();
            buf[0] = '\0';
            return 0;

        case CTRL('d'):
            if (line_len == 0)
            {
                flush_output();
                return -1; // EOF
            }
            delete_at(prompt_len);
            break;

        case CTRL('a'):
        case KEY_HOME:
            line_pos = 0;
            refresh_line(prompt_len);
            break;

        case CTRL('e'):
        case KEY_END:
            line_pos = line_len;
            refresh_line(prompt_len);
            break;

        case CTRL('k'):
            line_len = line_pos;
            line_buf[line_len] = '\0';
            refresh_line(prompt_len);
            break;

        case CTRL('u'):
            if (line_pos > 0)
            {
                int removed = line_pos;
                for (int i = 0; i < line_len - line_pos; i++)
                    line_buf[i] = line_buf[line_pos + i];
                line_len -= removed;
                line_pos = 0;
                line_buf[line_len] = '\0';
                redraw_full(prompt_len);
            }
            break;

        case CTRL('w'):
            if (line_pos > 0)
            {
                int old_pos = line_pos;
                // skip trailing spaces, then the word
                while (line_pos > 0 && line_buf[line_pos - 1] == ' ')
                    line_pos--;
                while (line_pos > 0 && line_buf[line_pos - 1] != ' ')
                    line_pos--;

                int removed = old_pos - line_pos;
                for (int i = line_pos; i < line_len - removed; i++)
                    line_buf[i] = line_buf[i + removed];
                line_len -= removed;
                line_buf[line_len] = '\0';
                redraw_full(prompt_len);
            }
            break;

        case CTRL('l'):
            // clear the screen; refresh_line draws the prompt and the line
            emit_clear_home();
            cursor_row = 0;
            redraw_full(prompt_len);
            break;

        case '\b':
        case 0x7f:
            delete_before(prompt_len);
            break;

        case KEY_DELETE:
            delete_at(prompt_len);
            break;

        case KEY_LEFT:
            if (line_pos > 0)
            {
                line_pos--;
                refresh_line(prompt_len);
            }
            break;

        case KEY_RIGHT:
            if (line_pos < line_len)
            {
                line_pos++;
                refresh_line(prompt_len);
            }
            break;

        case KEY_UP:
        {
            const char *entry = history_prev(line_buf);
            if (entry)
                line_replace(entry, prompt_len);
            break;
        }

        case KEY_DOWN:
        {
            const char *entry = history_next();
            if (entry)
                line_replace(entry, prompt_len);
            break;
        }

        default:
            // a printable character: insert at the cursor
            if (key >= 32 && key < 256 && key != 0x7f && line_len < LINE_MAX - 1)
            {
                for (int i = line_len; i > line_pos; i--)
                    line_buf[i] = line_buf[i - 1];
                line_buf[line_pos] = (char)key;
                line_pos++;
                line_len++;
                line_buf[line_len] = '\0';
                refresh_line(prompt_len);
            }
            break;
        }
    }
}

/**
 * read a line from input that is not a terminal: the bytes up to a
 * newline, without editing.
 * @return the length, or -1 at the end of input with nothing read.
 */
static int read_line_plain(char *buf, int size)
{
    int len = 0;
    for (;;)
    {
        int c = read_byte(-1);
        if (c < 0)
            return len > 0 ? len : -1;
        if (c == '\n')
            break;
        if (len < size - 1)
            buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return len;
}

int line_read(char *buf, int size)
{
    struct termios saved;
    if (tcgetattr(0, &saved) != 0)
        return read_line_plain(buf, size);

    // raw only while a line is edited: the commands the shell runs get the
    // terminal as it was (line input, echo, Ctrl+C)
    struct termios raw = saved;
    raw.c_iflag &= ~(tcflag_t)(ICRNL | INLCR | IGNCR);
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN | XKEYEVENTS | XKEYRAW);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);

    int len = read_line_raw(buf, size);
    tcsetattr(0, TCSANOW, &saved);
    return len;
}

void line_set_prompt_len(int len)
{
    current_prompt_len = len;
}

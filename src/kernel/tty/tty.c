#include "tty.h"

/**
 * @file tty.c
 * @brief terminal state machine: ANSI escape parsing, cursor handling, output.
 *
 * this is the logic that used to live in console.c, lifted onto a tty_t so it
 * no longer talks to the VGA driver directly. all cell access goes through the
 * backend ops, so the same parser drives any character-cell device.
 */

/**
 * reset the CSI parser for a fresh escape sequence.
 */
static void csi_reset(tty_t *tty)
{
    tty->csi_param_count = 0;
    tty->csi_current_param = 0;
    for (int i = 0; i < TTY_CSI_PARAMS_MAX; i++)
    {
        tty->csi_params[i] = 0;
    }
}

/**
 * push the current fg/bg to the backend, applying the bold flag by promoting
 * the foreground to its bright variant (index + 8). backends without colour
 * (set_colors == NULL) are left alone.
 */
static void apply_colors(tty_t *tty)
{
    if (!tty->backend->set_colors)
    {
        return;
    }

    uint8_t fg = tty->fg;
    if (tty->bold && fg < 8)
    {
        fg += 8; // bright variant
    }
    tty->backend->set_colors(fg, tty->bg);
}

/**
 * apply an SGR (ESC[...m) sequence to the terminal colour/attribute state.
 * supports reset, bold, and the 8 normal + 8 bright fg/bg colours.
 */
static void apply_sgr(tty_t *tty)
{
    // a bare ESC[m means ESC[0m (reset).
    for (int i = 0; i < tty->csi_param_count; i++)
    {
        int p = tty->csi_params[i];

        if (p == 0)
        {
            // reset to defaults.
            tty->fg = TTY_COLOR_DEFAULT_FG;
            tty->bg = TTY_COLOR_DEFAULT_BG;
            tty->bold = false;
        }
        else if (p == 1)
        {
            tty->bold = true;
        }
        else if (p == 22)
        {
            tty->bold = false;
        }
        else if (p >= 30 && p <= 37)
        {
            tty->fg = (uint8_t)(p - 30);
        }
        else if (p == 39)
        {
            tty->fg = TTY_COLOR_DEFAULT_FG;
        }
        else if (p >= 40 && p <= 47)
        {
            tty->bg = (uint8_t)(p - 40);
        }
        else if (p == 49)
        {
            tty->bg = TTY_COLOR_DEFAULT_BG;
        }
        else if (p >= 90 && p <= 97)
        {
            tty->fg = (uint8_t)(p - 90 + 8); // bright foreground
        }
        else if (p >= 100 && p <= 107)
        {
            tty->bg = (uint8_t)(p - 100 + 8); // bright background
        }
    }

    apply_colors(tty);
}

/**
 * execute a CSI sequence identified by its final character.
 */
static void csi_dispatch(tty_t *tty, char final)
{
    const tty_backend_t *be = tty->backend;

    // finalize the parameter currently being accumulated.
    if (tty->csi_param_count < TTY_CSI_PARAMS_MAX)
    {
        tty->csi_params[tty->csi_param_count] = tty->csi_current_param;
        tty->csi_param_count++;
    }

    uint8_t row, col;
    be->get_cursor(&row, &col);

    switch (final)
    {
    case 'A': // cursor up
    {
        int n = tty->csi_params[0] ? tty->csi_params[0] : 1;
        if (n > (int)row)
            n = (int)row;
        be->set_cursor((uint8_t)(row - n), col);
        break;
    }

    case 'B': // cursor down
    {
        int n = tty->csi_params[0] ? tty->csi_params[0] : 1;
        if (row + n >= (int)be->rows)
            n = (int)be->rows - 1 - row;
        be->set_cursor((uint8_t)(row + n), col);
        break;
    }

    case 'C': // cursor forward (right)
    {
        int n = tty->csi_params[0] ? tty->csi_params[0] : 1;
        if (col + n >= (int)be->cols)
            n = (int)be->cols - 1 - col;
        be->set_cursor(row, (uint8_t)(col + n));
        break;
    }

    case 'D': // cursor back (left)
    {
        int n = tty->csi_params[0] ? tty->csi_params[0] : 1;
        if (n > (int)col)
            n = (int)col;
        be->set_cursor(row, (uint8_t)(col - n));
        break;
    }

    case 'G': // cursor horizontal absolute (column), 1-based
    {
        int n = tty->csi_params[0] ? tty->csi_params[0] : 1;
        if (n < 1)
            n = 1;
        if (n > (int)be->cols)
            n = (int)be->cols;
        be->set_cursor(row, (uint8_t)(n - 1));
        break;
    }

    case 'H': // cursor position (row;col), 1-based
    case 'f': // same as H
    {
        int r = tty->csi_params[0] ? tty->csi_params[0] : 1;
        int c = (tty->csi_param_count >= 2 && tty->csi_params[1]) ? tty->csi_params[1] : 1;
        if (r < 1)
            r = 1;
        if (r > (int)be->rows)
            r = (int)be->rows;
        if (c < 1)
            c = 1;
        if (c > (int)be->cols)
            c = (int)be->cols;
        be->set_cursor((uint8_t)(r - 1), (uint8_t)(c - 1));
        break;
    }

    case 'J': // erase in display
    {
        int mode = tty->csi_params[0];
        if (mode == 2)
        {
            be->clear();
        }
        else if (mode == 0)
        {
            // from cursor to end of screen
            be->get_cursor(&row, &col);
            for (uint8_t c2 = col; c2 < be->cols; c2++)
                be->put_at(' ', row, c2);
            for (uint8_t r = (uint8_t)(row + 1); r < be->rows; r++)
                for (uint8_t c2 = 0; c2 < be->cols; c2++)
                    be->put_at(' ', r, c2);
        }
        break;
    }

    case 'K': // erase in line
    {
        int mode = tty->csi_params[0];
        be->get_cursor(&row, &col);
        if (mode == 0)
        {
            // from cursor to end of line
            for (uint8_t c2 = col; c2 < be->cols; c2++)
                be->put_at(' ', row, c2);
        }
        else if (mode == 1)
        {
            // from start of line to cursor
            for (uint8_t c2 = 0; c2 <= col; c2++)
                be->put_at(' ', row, c2);
        }
        else if (mode == 2)
        {
            // entire line
            for (uint8_t c2 = 0; c2 < be->cols; c2++)
                be->put_at(' ', row, c2);
        }
        break;
    }

    case 'm': // SGR: select graphic rendition (colours / attributes)
        apply_sgr(tty);
        break;

    default:
        // unrecognized sequence: silently ignore
        break;
    }
}

/**
 * draw a normal (non-escape) character, handling newline, carriage return,
 * backspace, and cell advancement with scrolling.
 */
static void tty_output_char(tty_t *tty, char c)
{
    const tty_backend_t *be = tty->backend;
    uint8_t row, col;

    switch (c)
    {
    case '\n':
        be->get_cursor(&row, &col);
        row++;
        if (row >= be->rows)
        {
            be->scroll();
            be->get_cursor(&row, &col);
        }
        be->set_cursor(row, 0);
        break;

    case '\r':
        be->get_cursor(&row, &col);
        be->set_cursor(row, 0);
        break;

    case '\b':
        be->get_cursor(&row, &col);
        if (col > 0)
        {
            col--;
            be->put_at(' ', row, col);
            be->set_cursor(row, col);
        }
        break;

    default:
    {
        be->get_cursor(&row, &col);
        be->put_at(c, row, col);
        col++;
        if (col >= be->cols)
        {
            col = 0;
            row++;
        }
        if (row >= be->rows)
        {
            be->scroll();
            row = (uint8_t)(be->rows - 1);
        }
        be->set_cursor(row, col);
        break;
    }
    }
}

void tty_init(tty_t *tty, const tty_backend_t *backend)
{
    tty->backend = backend;
    tty->state = TTY_STATE_NORMAL;
    tty->mode = TTY_MODE_COOKED;
    tty->fg = TTY_COLOR_DEFAULT_FG;
    tty->bg = TTY_COLOR_DEFAULT_BG;
    tty->bold = false;
    csi_reset(tty);
    apply_colors(tty);

    // draw the initial caret at the home position.
    if (backend->draw_cursor)
    {
        uint8_t row, col;
        backend->get_cursor(&row, &col);
        backend->draw_cursor(row, col, true);
    }
}

/**
 * process one output byte through the parser and draw its effect. does not
 * touch the caret overlay; the public tty_putchar brackets this with the
 * caret erase/redraw so the caret never corrupts drawn cells.
 */
static void tty_process(tty_t *tty, char c)
{
    switch (tty->state)
    {
    case TTY_STATE_NORMAL:
        if (c == '\033')
        {
            tty->state = TTY_STATE_ESC;
            return;
        }
        break;

    case TTY_STATE_ESC:
        if (c == '[')
        {
            tty->state = TTY_STATE_CSI;
            csi_reset(tty);
            return;
        }
        // not a CSI sequence: drop back to normal and print c literally.
        tty->state = TTY_STATE_NORMAL;
        break;

    case TTY_STATE_CSI:
        if (c == '?')
        {
            tty->state = TTY_STATE_CSI_PRIV;
            return;
        }
        else if (c >= '0' && c <= '9')
        {
            tty->csi_current_param = tty->csi_current_param * 10 + (c - '0');
            return;
        }
        else if (c == ';')
        {
            if (tty->csi_param_count < TTY_CSI_PARAMS_MAX)
            {
                tty->csi_params[tty->csi_param_count] = tty->csi_current_param;
                tty->csi_param_count++;
            }
            tty->csi_current_param = 0;
            return;
        }
        else if (c >= 0x40 && c <= 0x7E)
        {
            csi_dispatch(tty, c);
            tty->state = TTY_STATE_NORMAL;
            return;
        }
        else
        {
            // unexpected byte: abort the sequence.
            tty->state = TTY_STATE_NORMAL;
            return;
        }

    case TTY_STATE_CSI_PRIV:
        if (c >= 0x40 && c <= 0x7E)
        {
            // final byte: silently discard DEC private mode sequences
            // (e.g. ?25l hide cursor, ?25h show cursor).
            tty->state = TTY_STATE_NORMAL;
            return;
        }
        // intermediate bytes (digits, ;): consume them.
        return;
    }

    tty_output_char(tty, c);
}

void tty_putchar(tty_t *tty, char c)
{
    const tty_backend_t *be = tty->backend;
    uint8_t row, col;

    // erase the caret at the current position so it isn't baked into a cell
    // we're about to draw, process the byte, then redraw the caret wherever the
    // cursor ended up. backends with no software caret (draw_cursor == NULL,
    // e.g. the VGA hardware cursor) skip the overlay entirely.
    if (be->draw_cursor)
    {
        be->get_cursor(&row, &col);
        be->draw_cursor(row, col, false);
    }

    tty_process(tty, c);

    if (be->draw_cursor)
    {
        be->get_cursor(&row, &col);
        be->draw_cursor(row, col, true);
    }

    if (be->flush)
    {
        be->flush();
    }
}

void tty_write(tty_t *tty, const char *str)
{
    while (*str)
    {
        tty_putchar(tty, *str++);
    }
}

uint8_t tty_set_mode(tty_t *tty, uint8_t mode)
{
    uint8_t prev = tty->mode;
    tty->mode = mode;
    return prev;
}

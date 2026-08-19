#include "console.h"

#include "../../drivers/video/vga/vga.h"
#include "../../drivers/keyboard/keyboard.h"

#define CONSOLE_VGA_WIDTH 80u
#define CONSOLE_VGA_HEIGHT 25u

// ANSI escape sequence parser state
typedef enum
{
    STATE_NORMAL,   // normal character output
    STATE_ESC,      // received ESC (0x1B)
    STATE_CSI,      // received ESC [ (CSI sequence)
    STATE_CSI_PRIV, // received ESC [ ? (DEC private mode sequence)
} console_state_t;

static console_state_t console_state = STATE_NORMAL;

// CSI parameter buffer
#define CSI_PARAMS_MAX 4
static int csi_params[CSI_PARAMS_MAX];
static int csi_param_count;
static int csi_current_param;

/**
 * reset the CSI parser state for a new sequence.
 */
static void csi_reset(void)
{
    csi_param_count = 0;
    csi_current_param = 0;
    for (int i = 0; i < CSI_PARAMS_MAX; i++)
        csi_params[i] = 0;
}

/**
 * execute a CSI sequence identified by the final character.
 */
static void csi_dispatch(char final)
{
    // finalize the last parameter being parsed
    if (csi_param_count < CSI_PARAMS_MAX)
    {
        csi_params[csi_param_count] = csi_current_param;
        csi_param_count++;
    }

    uint8_t row, col;
    vga_get_cursor(&row, &col);

    switch (final)
    {
    case 'A': // Cursor Up
    {
        int n = csi_params[0] ? csi_params[0] : 1;
        if (n > (int)row)
            n = (int)row;
        vga_set_cursor((uint8_t)(row - n), col);
        break;
    }

    case 'B': // Cursor Down
    {
        int n = csi_params[0] ? csi_params[0] : 1;
        if (row + n >= (int)CONSOLE_VGA_HEIGHT)
            n = (int)CONSOLE_VGA_HEIGHT - 1 - row;
        vga_set_cursor((uint8_t)(row + n), col);
        break;
    }

    case 'C': // Cursor Forward (right)
    {
        int n = csi_params[0] ? csi_params[0] : 1;
        if (col + n >= (int)CONSOLE_VGA_WIDTH)
            n = (int)CONSOLE_VGA_WIDTH - 1 - col;
        vga_set_cursor(row, (uint8_t)(col + n));
        break;
    }

    case 'D': // Cursor Back (left)
    {
        int n = csi_params[0] ? csi_params[0] : 1;
        if (n > (int)col)
            n = (int)col;
        vga_set_cursor(row, (uint8_t)(col - n));
        break;
    }

    case 'G': // Cursor Horizontal Absolute (column)
    {
        int n = csi_params[0] ? csi_params[0] : 1;
        if (n < 1)
            n = 1;
        if (n > (int)CONSOLE_VGA_WIDTH)
            n = (int)CONSOLE_VGA_WIDTH;
        vga_set_cursor(row, (uint8_t)(n - 1)); // 1-based
        break;
    }

    case 'H': // Cursor Position (row;col)  1-based
    case 'f': // same as H
    {
        int r = csi_params[0] ? csi_params[0] : 1;
        int c = (csi_param_count >= 2 && csi_params[1]) ? csi_params[1] : 1;
        if (r < 1)
            r = 1;
        if (r > (int)CONSOLE_VGA_HEIGHT)
            r = (int)CONSOLE_VGA_HEIGHT;
        if (c < 1)
            c = 1;
        if (c > (int)CONSOLE_VGA_WIDTH)
            c = (int)CONSOLE_VGA_WIDTH;
        vga_set_cursor((uint8_t)(r - 1), (uint8_t)(c - 1));
        break;
    }

    case 'J': // Erase in Display
    {
        int mode = csi_params[0];
        if (mode == 2)
        {
            // clear entire screen
            vga_clear();
        }
        else if (mode == 0)
        {
            // clear from cursor to end of screen
            vga_get_cursor(&row, &col);
            for (uint8_t c2 = col; c2 < CONSOLE_VGA_WIDTH; c2++)
                vga_put_at(' ', row, c2);
            for (uint8_t r = (uint8_t)(row + 1); r < CONSOLE_VGA_HEIGHT; r++)
                for (uint8_t c2 = 0; c2 < CONSOLE_VGA_WIDTH; c2++)
                    vga_put_at(' ', r, c2);
        }
        break;
    }

    case 'K': // Erase in Line
    {
        int mode = csi_params[0];
        vga_get_cursor(&row, &col);
        if (mode == 0)
        {
            // clear from cursor to end of line
            for (uint8_t c2 = col; c2 < CONSOLE_VGA_WIDTH; c2++)
                vga_put_at(' ', row, c2);
        }
        else if (mode == 1)
        {
            // clear from start of line to cursor
            for (uint8_t c2 = 0; c2 <= col; c2++)
                vga_put_at(' ', row, c2);
        }
        else if (mode == 2)
        {
            // clear entire line
            for (uint8_t c2 = 0; c2 < CONSOLE_VGA_WIDTH; c2++)
                vga_put_at(' ', row, c2);
        }
        break;
    }

    default:
        // unrecognized sequence: silently ignore
        break;
    }
}

void console_initialize(void)
{
    vga_initialize();
    console_state = STATE_NORMAL;
}

void console_putchar(char c)
{
    switch (console_state)
    {
    case STATE_NORMAL:
        if (c == '\033')
        {
            console_state = STATE_ESC;
            return;
        }
        break;

    case STATE_ESC:
        if (c == '[')
        {
            console_state = STATE_CSI;
            csi_reset();
            return;
        }
        // not a CSI sequence: output the ESC and the character literally
        console_state = STATE_NORMAL;
        // fall through to print c
        break;

    case STATE_CSI:
        if (c == '?')
        {
            // DEC private mode: consume until final byte
            console_state = STATE_CSI_PRIV;
            return;
        }
        else if (c >= '0' && c <= '9')
        {
            // accumulate numeric parameter
            csi_current_param = csi_current_param * 10 + (c - '0');
            return;
        }
        else if (c == ';')
        {
            // parameter separator
            if (csi_param_count < CSI_PARAMS_MAX)
            {
                csi_params[csi_param_count] = csi_current_param;
                csi_param_count++;
            }
            csi_current_param = 0;
            return;
        }
        else if (c >= 0x40 && c <= 0x7E)
        {
            // final byte: dispatch the sequence
            csi_dispatch(c);
            console_state = STATE_NORMAL;
            return;
        }
        else
        {
            // unexpected character: abort sequence
            console_state = STATE_NORMAL;
            return;
        }

    case STATE_CSI_PRIV:
        if (c >= 0x40 && c <= 0x7E)
        {
            // final byte silently discard DEC private mode sequences
            // (e.g., ?25l = hide cursor, ?25h = show cursor)
            console_state = STATE_NORMAL;
            return;
        }
        // intermediate bytes (digits, ;) just consume them
        return;
    }

    // normal character output
    uint8_t row, col;

    switch (c)
    {
    case '\n':
        vga_get_cursor(&row, &col);
        row++;
        if (row >= CONSOLE_VGA_HEIGHT)
        {
            vga_scroll();
            vga_get_cursor(&row, &col);
        }
        vga_set_cursor(row, 0);
        break;

    case '\r':
        vga_get_cursor(&row, &col);
        vga_set_cursor(row, 0);
        break;

    case '\b':
        vga_get_cursor(&row, &col);
        if (col > 0)
        {
            col--;
            vga_set_cursor(row, col);
            vga_put_at(' ', row, col);
            vga_set_cursor(row, col);
        }
        break;

    default:
        vga_putchar(c);
        break;
    }
}

void console_write(const char *str)
{
    while (*str)
    {
        console_putchar(*str++);
    }
}

int console_readline(char *buffer, int size)
{
    int length = 0;

    while (1)
    {
        while (!keyboard_available())
        {
        }

        char c = keyboard_getchar();

        if (c == '\n')
        {
            buffer[length] = 0;
            console_putchar('\n');
            return length;
        }

        if (c == '\b')
        {
            if (length > 0)
            {
                length--;
                console_putchar('\b');
            }

            continue;
        }

        if (length < size - 1)
        {
            buffer[length++] = c;
            console_putchar(c);
        }
    }
}
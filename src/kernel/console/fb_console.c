#include "fb_console.h"

#include "drivers/video/fb/fb.h"
#include "drivers/video/fb/fb_text.h"

/**
 * @file fb_console.c
 * @brief adapts the framebuffer text grid to the tty backend interface.
 *
 * the tty state machine drives a tty_backend_t (put_at/scroll/clear/cursor).
 * these adapters forward to fb_text, supplying the default colours the current
 * parser does not carry (SGR colour handling arrives in FB4). the backend's
 * rows/cols are filled in at init from the runtime grid size, so this is a
 * live struct rather than a compile-time constant like the VGA one.
 */

// default look: light grey on near-black, matching the old text console feel.
#define FB_DEFAULT_FG 0x00CCCCCC
#define FB_DEFAULT_BG 0x00000000

static tty_backend_t fb_backend;

static void fb_be_put_at(char c, uint8_t row, uint8_t col)
{
    fb_text_draw_glyph(row, col, c, FB_DEFAULT_FG, FB_DEFAULT_BG);
}

static void fb_be_scroll(void)
{
    fb_text_scroll();
}

static void fb_be_clear(void)
{
    fb_text_clear();
}

static void fb_be_set_cursor(uint8_t row, uint8_t col)
{
    fb_text_set_cursor(row, col);
}

static void fb_be_get_cursor(uint8_t *row, uint8_t *col)
{
    fb_text_get_cursor(row, col);
}

const tty_backend_t *fb_console_backend(void)
{
    // bring up the text grid (native scale, small margin), then describe it to
    // the tty via the backend ops.
    fb_text_set_default_bg(FB_DEFAULT_BG);
    fb_text_init(1, 8);

    fb_backend.put_at = fb_be_put_at;
    fb_backend.scroll = fb_be_scroll;
    fb_backend.clear = fb_be_clear;
    fb_backend.set_cursor = fb_be_set_cursor;
    fb_backend.get_cursor = fb_be_get_cursor;
    fb_backend.rows = fb_text_rows();
    fb_backend.cols = fb_text_cols();

    return &fb_backend;
}

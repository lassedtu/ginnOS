#include "fb_text.h"
#include "fb.h"
#include "font.h"

/**
 * @file fb_text.c
 * @brief text grid renderer over the framebuffer.
 */

static uint32_t cell_w;   // cell width in pixels (glyph width * scale)
static uint32_t cell_h;   // cell height in pixels (glyph height * scale)
static uint32_t scale_f;  // integer magnification
static uint32_t origin_x; // left margin in pixels
static uint32_t origin_y; // top margin in pixels
static uint8_t grid_rows;
static uint8_t grid_cols;

static uint8_t cursor_row;
static uint8_t cursor_col;

static uint32_t default_bg; // packed background pixel

/**
 * convert a 0x00RRGGBB colour to a framebuffer-packed pixel.
 */
static uint32_t pack(uint32_t rgb)
{
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFF);
    uint8_t b = (uint8_t)(rgb & 0xFF);
    return fb_pack_rgb(r, g, b);
}

void fb_text_set_default_bg(uint32_t bg)
{
    default_bg = pack(bg);
}

void fb_text_init(uint32_t scale, uint32_t margin)
{
    const fb_info_t *info = fb_get_info();

    scale_f = scale ? scale : 1;
    cell_w = FONT_GLYPH_WIDTH * scale_f;
    cell_h = FONT_GLYPH_HEIGHT * scale_f;
    origin_x = margin;
    origin_y = margin;

    // how many whole cells fit inside the margins.
    uint32_t usable_w = (info->width > 2 * margin) ? info->width - 2 * margin : 0;
    uint32_t usable_h = (info->height > 2 * margin) ? info->height - 2 * margin : 0;
    grid_cols = (uint8_t)(usable_w / cell_w);
    grid_rows = (uint8_t)(usable_h / cell_h);

    cursor_row = 0;
    cursor_col = 0;
    default_bg = fb_pack_rgb(0, 0, 0);

    fb_text_clear();
}

uint8_t fb_text_rows(void)
{
    return grid_rows;
}

uint8_t fb_text_cols(void)
{
    return grid_cols;
}

void fb_text_draw_glyph(uint8_t row, uint8_t col, char ch, uint32_t fg, uint32_t bg)
{
    if (row >= grid_rows || col >= grid_cols)
    {
        return;
    }

    uint32_t px = origin_x + (uint32_t)col * cell_w;
    uint32_t py = origin_y + (uint32_t)row * cell_h;
    uint32_t fg_pixel = pack(fg);
    uint32_t bg_pixel = pack(bg);

    // paint the cell background first.
    fb_fill_rect(px, py, cell_w, cell_h, bg_pixel);

    const uint8_t (*glyph)[FONT_GLYPH_BYTES_PER_ROW] = font_glyphs[(uint8_t)ch];

    for (uint32_t gy = 0; gy < FONT_GLYPH_HEIGHT; gy++)
    {
        for (uint32_t gx = 0; gx < FONT_GLYPH_WIDTH; gx++)
        {
            uint8_t byte = glyph[gy][gx / 8];
            if (!(byte & (1u << (7 - (gx % 8)))))
            {
                continue; // clear pixel
            }

            // draw a scale x scale block for this source pixel.
            uint32_t dx = px + gx * scale_f;
            uint32_t dy = py + gy * scale_f;
            if (scale_f == 1)
            {
                fb_put_pixel(dx, dy, fg_pixel);
            }
            else
            {
                fb_fill_rect(dx, dy, scale_f, scale_f, fg_pixel);
            }
        }
    }
}

void fb_text_clear(void)
{
    fb_clear(default_bg);
    cursor_row = 0;
    cursor_col = 0;
}

void fb_text_scroll(void)
{
    const fb_info_t *info = fb_get_info();

    // move every text row up by one cell height.
    uint32_t scrolled_h = (uint32_t)(grid_rows - 1) * cell_h;
    fb_copy_rect(origin_x, origin_y,
                 origin_x, origin_y + cell_h,
                 (uint32_t)grid_cols * cell_w, scrolled_h);

    // clear the newly exposed bottom row.
    uint32_t last_y = origin_y + scrolled_h;
    fb_fill_rect(origin_x, last_y, (uint32_t)grid_cols * cell_w, cell_h, default_bg);

    (void)info;
}

void fb_text_set_cursor(uint8_t row, uint8_t col)
{
    cursor_row = row;
    cursor_col = col;
}

void fb_text_get_cursor(uint8_t *row, uint8_t *col)
{
    *row = cursor_row;
    *col = cursor_col;
}

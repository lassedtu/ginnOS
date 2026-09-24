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

// 16-entry ANSI palette as 0x00RRGGBB. indices 0-7 normal, 8-15 bright.
static const uint32_t ansi_palette[16] = {
    0x00000000, // 0 black
    0x00CD0000, // 1 red
    0x0000CD00, // 2 green
    0x00CDCD00, // 3 yellow
    0x000000EE, // 4 blue
    0x00CD00CD, // 5 magenta
    0x0000CDCD, // 6 cyan
    0x00CCCCCC, // 7 light grey
    0x00555555, // 8 dark grey
    0x00FF5555, // 9 bright red
    0x0055FF55, // 10 bright green
    0x00FFFF55, // 11 bright yellow
    0x005555FF, // 12 bright blue
    0x00FF55FF, // 13 bright magenta
    0x0055FFFF, // 14 bright cyan
    0x00FFFFFF, // 15 white
};

static uint32_t active_fg = 0x00CCCCCC; // active foreground (0x00RRGGBB)
static uint32_t active_bg = 0x00000000; // active background (0x00RRGGBB)

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

void fb_text_set_colors(uint8_t fg, uint8_t bg)
{
    active_fg = ansi_palette[fg & 0x0F];
    active_bg = ansi_palette[bg & 0x0F];
}

void fb_text_put(uint8_t row, uint8_t col, char ch)
{
    fb_text_draw_glyph(row, col, ch, active_fg, active_bg);
}

void fb_text_draw_cursor(uint8_t row, uint8_t col, bool visible)
{
    if (row >= grid_rows || col >= grid_cols)
    {
        return;
    }

    uint32_t px = origin_x + (uint32_t)col * cell_w;
    // an underline caret occupying the bottom two (scaled) scanlines of the cell.
    uint32_t bar_h = 2 * scale_f;
    uint32_t py = origin_y + (uint32_t)row * cell_h + cell_h - bar_h;

    uint32_t colour = visible ? fb_pack_rgb((uint8_t)((active_fg >> 16) & 0xFF),
                                            (uint8_t)((active_fg >> 8) & 0xFF),
                                            (uint8_t)(active_fg & 0xFF))
                              : default_bg;
    fb_fill_rect(px, py, cell_w, bar_h, colour);
}

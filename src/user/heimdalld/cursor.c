/**
 * @file cursor.c
 * @brief the pointer shapes, drawn from text pictures at start-up.
 *
 * Lucide has no pointer shapes, so they are made here: '#' is black, '.'
 * is white, a space is see-through.
 */

#include "heimdalld.h"

#include <string.h>
#include <sys/fb.h>
#include <sys/ioctl.h>

typedef struct
{
    gfx_buffer_t *image;
    int32_t hot_x; // the pixel that points
    int32_t hot_y;
} cursor_t;

static cursor_t cursors[CURSOR_COUNT];

// what the card's cursor shows now
static struct
{
    bool up;        // an image is there
    uint32_t shape;
    int32_t cut_x;  // columns cut off at the left edge of the screen
    int32_t cut_y;
    int32_t x;      // its top left corner
    int32_t y;
} hw;

static fb_cursor_t upload; // 16 KiB: not on the stack

static const char *const arrow[] = {
    "#           ", "##          ", "#.#         ", "#..#        ", "#...#       ",
    "#....#      ", "#.....#     ", "#......#    ", "#.......#   ", "#........#  ",
    "#.........# ", "#..........#", "#......#####", "#...#..#    ", "#..##..#    ",
    "#.#  #..#   ", "##   #..#   ", "#     #..#  ", "      #..#  ", "       ##   ",
};

static const char *const beam[] = {
    ".......", ".##.##.", "...#...", "  .#.  ", "  .#.  ", "  .#.  ", "  .#.  ", "  .#.  ",
    "  .#.  ", "  .#.  ", "  .#.  ", "  .#.  ", "  .#.  ", "  .#.  ", "...#...", ".##.##.",
    ".......",
};

static const char *const hand[] = {
    "     ##         ", "    #..#        ", "    #..#        ", "    #..#        ",
    "    #..###      ", "    #..#..###   ", "    #..#..#..## ", " ## #..#..#..#.#",
    "#..##........#.#", "#...#..........#", " #.............#", "  #............#",
    "  #...........# ", "   #..........# ", "   #.........#  ", "    #........#  ",
    "    #........#  ", "    ##########  ",
};

static const char *const resize_h[] = {
    "    #           #    ", "   ##           ##   ", "  #.#############.#  ",
    " #.................# ", "#...................#", " #.................# ",
    "  #.#############.#  ", "   ##           ##   ", "    #           #    ",
};

static void set_pixel(gfx_buffer_t *b, int32_t x, int32_t y, char c)
{
    if (c == '#')
        *gfx_pixel_at(b, x, y) = gfx_rgb(0, 0, 0);
    else if (c == '.')
        *gfx_pixel_at(b, x, y) = gfx_rgb(255, 255, 255);
}

static void from_rows(cursor_t *cur, const char *const *rows, int32_t count, int32_t hx,
                      int32_t hy, bool transpose)
{
    int32_t w = (int32_t)strlen(rows[0]);
    int32_t h = count;
    cur->image = transpose ? gfx_buffer_new(h, w) : gfx_buffer_new(w, h);
    if (!cur->image)
        return;
    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++)
            set_pixel(cur->image, transpose ? y : x, transpose ? x : y, rows[y][x]);
    cur->hot_x = transpose ? hy : hx;
    cur->hot_y = transpose ? hx : hy;
}

/**
 * a diagonal double arrow, 15 x 15, from top left to bottom right (or,
 * mirrored, from top right to bottom left).
 */
static void diagonal(cursor_t *cur, bool mirror)
{
    const int32_t n = 15;
    cur->image = gfx_buffer_new(n, n);
    if (!cur->image)
        return;
    for (int32_t y = 0; y < n; y++)
    {
        for (int32_t x = 0; x < n; x++)
        {
            int32_t d = x > y ? x - y : y - x;
            int32_t s = x + y;
            char c = ' ';
            if (d <= 1)
                c = '.'; // the shaft
            else if (d == 2)
                c = '#';
            if (s <= 6 && x <= 5 && y <= 5)
                c = (s == 6 || x == 0 || y == 0) ? '#' : '.'; // the top left head
            if (s >= 22 && x >= 9 && y >= 9)
                c = (s == 22 || x == n - 1 || y == n - 1) ? '#' : '.'; // the other head
            set_pixel(cur->image, mirror ? n - 1 - x : x, y, c);
        }
    }
    cur->hot_x = n / 2;
    cur->hot_y = n / 2;
}

/**
 * make a cursor scale times larger, each pixel a square, so it stays sharp.
 */
static void enlarge(cursor_t *cur, int32_t scale)
{
    if (!cur->image || scale <= 1)
        return;
    gfx_buffer_t *small = cur->image;
    cur->image = gfx_buffer_new(small->width * scale, small->height * scale);
    if (cur->image)
    {
        for (int32_t y = 0; y < cur->image->height; y++)
            for (int32_t x = 0; x < cur->image->width; x++)
                *gfx_pixel_at(cur->image, x, y) = *gfx_pixel_at(small, x / scale, y / scale);
    }
    cur->hot_x = cur->hot_x * scale + scale / 2;
    cur->hot_y = cur->hot_y * scale + scale / 2;
    gfx_buffer_free(small);
}

void cursor_init(int32_t scale)
{
    for (int i = 0; i < CURSOR_COUNT; i++)
    {
        gfx_buffer_free(cursors[i].image);
        cursors[i].image = NULL;
    }
    from_rows(&cursors[HEIM_CURSOR_ARROW], arrow, 20, 0, 0, false);
    from_rows(&cursors[HEIM_CURSOR_TEXT], beam, 17, 3, 8, false);
    from_rows(&cursors[HEIM_CURSOR_HAND], hand, 18, 5, 0, false);
    from_rows(&cursors[CURSOR_RESIZE_H], resize_h, 9, 10, 4, false);
    from_rows(&cursors[CURSOR_RESIZE_V], resize_h, 9, 10, 4, true);
    diagonal(&cursors[CURSOR_RESIZE_NWSE], false);
    diagonal(&cursors[CURSOR_RESIZE_NESW], true);
    for (int i = 0; i < CURSOR_COUNT; i++)
        enlarge(&cursors[i], scale);
    hw.up = false;
}

void cursor_hardware_reset(void)
{
    hw.up = false;
}

bool cursor_hardware(int fb_fd, uint32_t shape, int32_t x, int32_t y)
{
    shape = shape < CURSOR_COUNT ? shape : 0;
    const cursor_t *c = &cursors[shape];
    if (!c->image || c->image->width > FB_CURSOR_SIZE || c->image->height > FB_CURSOR_SIZE)
        return false;

    // the card takes no place left of or above the screen: cut the image
    int32_t left = x - c->hot_x;
    int32_t top = y - c->hot_y;
    int32_t cut_x = left < 0 ? -left : 0;
    int32_t cut_y = top < 0 ? -top : 0;
    left += cut_x;
    top += cut_y;

    if (!hw.up || hw.shape != shape || hw.cut_x != cut_x || hw.cut_y != cut_y)
    {
        // gfx pixels are premultiplied; the shapes are only opaque or
        // see-through, where both ways are the same
        memset(upload.pixels, 0, sizeof(upload.pixels));
        for (int32_t row = 0; row + cut_y < c->image->height; row++)
            for (int32_t col = 0; col + cut_x < c->image->width; col++)
                upload.pixels[row * FB_CURSOR_SIZE + col] =
                    *gfx_pixel_at(c->image, col + cut_x, row + cut_y);
        upload.shown = 1;
        upload.x = (uint32_t)left;
        upload.y = (uint32_t)top;
        if (ioctl(fb_fd, FBIO_CURSOR, &upload) != 0)
            return false;
        hw.up = true;
        hw.shape = shape;
        hw.cut_x = cut_x;
        hw.cut_y = cut_y;
        hw.x = left;
        hw.y = top;
        return true;
    }
    if (left != hw.x || top != hw.y)
    {
        fb_point_t at = {(uint32_t)left, (uint32_t)top};
        ioctl(fb_fd, FBIO_CURSOR_MOVE, &at);
        hw.x = left;
        hw.y = top;
    }
    return true;
}

gfx_rect_t cursor_rect(uint32_t shape, int32_t x, int32_t y)
{
    const cursor_t *c = &cursors[shape < CURSOR_COUNT ? shape : 0];
    if (!c->image)
        return gfx_rect(x, y, 0, 0);
    return gfx_rect(x - c->hot_x, y - c->hot_y, c->image->width, c->image->height);
}

void cursor_draw(gfx_buffer_t *buf, uint32_t shape, int32_t x, int32_t y)
{
    const cursor_t *c = &cursors[shape < CURSOR_COUNT ? shape : 0];
    if (c->image)
        gfx_blend(buf, x - c->hot_x, y - c->hot_y, c->image,
                  gfx_rect(0, 0, c->image->width, c->image->height), 255);
}

/**
 * @file gfxdemo.c
 * @brief draw a mock desktop with libgfx and bragi on /dev/fb0.
 *
 * usage: gfxdemo [image]
 *
 * with an image (PNG, JPEG or BMP), it is the wallpaper, scaled to the
 * screen. the text is in Inter and JetBrains Mono from /usr/share/fonts;
 * without them, grey bars take the place of the text.
 *
 * draws into a buffer in memory, then copies it to the screen in one
 * step (gfx_present), and waits for Enter. a test of the libraries on the
 * real framebuffer, until the compositor exists.
 */

#include <bragi.h>
#include <gfx.h>
#include <image.h>
#include <stdio.h>
#include <string.h>
#include <sys/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define FONTS "/usr/share/fonts/"

static bragi_face_t *title_face;
static bragi_face_t *body_face;
static bragi_face_t *mono_face;

static const char *const paragraph =
    "ginnOS draws this window with libgfx: premultiplied pixels, round corners "
    "with smooth edges, and soft shadows. The text comes from bragi, which uses "
    "TrueType fonts, puts each glyph at a quarter of a pixel, and keeps the "
    "glyphs it made in a cache. \xC3\x86\xC3\x98\xC3\x85 \xC3\xA6\xC3\xB8\xC3\xA5, "
    "caf\xC3\xA9, 12 \xE2\x82\xAC. When the text is too long for its box, it ends "
    "with an ellipsis, as this one does, because the box is not tall enough for "
    "all of it.";

static const char *const terminal[] = {
    "skl:/ $ ls /usr/share/fonts",
    "inter  jetbrains-mono",
    "skl:/ $ fbmodes",
    "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x94\x80\xE2\x94\xAC\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x94\x80\xE2\x94\x90",
    "\xE2\x94\x82 width \xE2\x94\x82 1280 \xE2\x94\x82",
    "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x94\x80\xE2\x94\xBC\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x94\x80\xE2\x94\xA4",
    "\xE2\x94\x82 bpp   \xE2\x94\x82   32 \xE2\x94\x82",
    "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x94\x80\xE2\x94\xB4\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x94\x80\xE2\x94\x98",
    "load \xE2\x96\x81\xE2\x96\x82\xE2\x96\x83\xE2\x96\x85\xE2\x96\x87\xE2\x96\x88"
    "\xE2\x96\x86\xE2\x96\x84\xE2\x96\x82 \xE2\x96\x91\xE2\x96\x92\xE2\x96\x93",
    "skl:/ $ _",
};

static uint32_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}

/**
 * load a face, or NULL (the demo then draws bars for text).
 */
static bragi_face_t *load_face(const char *path, float size)
{
    bragi_font_t *font = bragi_font_open(path);
    return font ? bragi_face_new(font, size) : NULL;
}

/**
 * a window: a soft shadow, the body, a title bar with a title and three
 * buttons. it gives the content area.
 */
static gfx_rect_t draw_window(gfx_buffer_t *buf, gfx_rect_t r, const char *title,
                              gfx_color_t bar, gfx_color_t body)
{
    // the shadow: rings of faint black, wider and fainter outwards
    for (int32_t i = 12; i > 0; i--)
    {
        gfx_rect_t s = gfx_rect(r.x - i, r.y - i + 6, r.w + 2 * i, r.h + 2 * i);
        gfx_fill_round_rect(buf, s, 10 + i, gfx_rgba(0, 0, 0, 6));
    }
    gfx_fill_round_rect(buf, r, 10, body);

    // the title bar: round at the top only (the lower corners are covered)
    gfx_fill_round_rect(buf, gfx_rect(r.x, r.y, r.w, 36), 10, bar);
    gfx_fill_rect(buf, gfx_rect(r.x, r.y + 26, r.w, 10), bar);
    gfx_line(buf, r.x, r.y + 36, r.x + r.w - 1, r.y + 36, gfx_rgba(0, 0, 0, 60));
    if (title_face)
    {
        int32_t w = bragi_measure(title_face, title, -1);
        int32_t baseline = r.y + (36 + bragi_ascent(title_face) - bragi_descent(title_face)) / 2;
        bragi_draw(buf, title_face, r.x + (r.w - w) / 2, baseline, title, -1,
                   gfx_rgb(40, 42, 50));
    }

    // minimize, maximize, close
    static const uint8_t colors[3][3] = {{250, 190, 60}, {80, 200, 110}, {240, 90, 80}};
    for (int32_t i = 0; i < 3; i++)
    {
        int32_t x = r.x + r.w - 26 - (2 - i) * 24;
        gfx_fill_round_rect(buf, gfx_rect(x, r.y + 11, 14, 14), 7,
                            gfx_rgb(colors[i][0], colors[i][1], colors[i][2]));
    }
    return gfx_rect(r.x + 20, r.y + 52, r.w - 40, r.h - 68);
}

static void draw_text_window(gfx_buffer_t *buf, gfx_rect_t r)
{
    gfx_rect_t area = draw_window(buf, r, "About ginnOS", gfx_rgb(225, 228, 236),
                                  gfx_rgb(245, 245, 248));
    if (!body_face)
    {
        for (int32_t row = 0; row < 6; row++)
            gfx_fill_round_rect(buf, gfx_rect(area.x, area.y + row * 26,
                                              area.w * (10 - row) / 10, 12),
                                6, gfx_rgb(215, 218, 226));
        return;
    }
    int32_t lh = bragi_line_height(body_face);
    gfx_rect_t box = gfx_rect(area.x, area.y, area.w, area.h < 4 * lh ? area.h : 4 * lh);
    bragi_draw_box(buf, body_face, box, paragraph, BRAGI_ALIGN_LEFT, gfx_rgb(30, 32, 38));
}

static void draw_terminal_window(gfx_buffer_t *buf, gfx_rect_t r)
{
    gfx_rect_t area = draw_window(buf, r, "Terminal", gfx_rgb(200, 215, 245),
                                  gfx_rgb(22, 24, 30));
    if (!mono_face)
        return;
    int32_t cw;
    int32_t ch;
    bragi_cell_size(mono_face, &cw, &ch);
    int32_t rows = (int32_t)(sizeof(terminal) / sizeof(terminal[0]));
    for (int32_t row = 0; row < rows && (row + 1) * ch <= area.h; row++)
    {
        const char *s = terminal[row];
        const char *end = s + strlen(s);
        bool prompt = strncmp(s, "skl:", 4) == 0;
        for (int32_t col = 0; s < end && (col + 1) * cw <= area.w; col++)
        {
            uint32_t c = bragi_utf8_next(&s, end);
            gfx_color_t color = prompt && col < 7 ? gfx_rgb(120, 220, 140) : gfx_rgb(214, 218, 228);
            bragi_draw_cell(buf, mono_face, area.x + col * cw, area.y + row * ch, c, color);
        }
    }
}

int main(int argc, char **argv)
{
    int fd = fb_open();
    fb_var_screeninfo_t var;
    fb_fix_screeninfo_t fix;
    if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0)
    {
        printf("gfxdemo: no framebuffer\n");
        return 1;
    }
    void *fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (fb == MAP_FAILED)
    {
        printf("gfxdemo: cannot map the framebuffer\n");
        return 1;
    }
    gfx_format_t format = {
        (uint8_t)(var.bits_per_pixel / 8), (uint8_t)var.red.length,  (uint8_t)var.red.offset,
        (uint8_t)var.green.length,         (uint8_t)var.green.offset, (uint8_t)var.blue.length,
        (uint8_t)var.blue.offset,
    };

    int32_t w = (int32_t)var.xres;
    int32_t h = (int32_t)var.yres;
    gfx_buffer_t *screen = gfx_buffer_new(w, h);
    if (!screen)
    {
        printf("gfxdemo: out of memory\n");
        return 1;
    }

    title_face = load_face(FONTS "inter/Inter-SemiBold.ttf", 13.0f);
    body_face = load_face(FONTS "inter/Inter-Regular.ttf", 15.0f);
    mono_face = load_face(FONTS "jetbrains-mono/JetBrainsMono-Regular.ttf", 14.0f);

    // the wallpaper, and a panel at the bottom
    gfx_fill_gradient(screen, gfx_rect(0, 0, w, h), gfx_rgb(40, 60, 110), gfx_rgb(120, 70, 130));
    if (argc > 1)
    {
        const char *error = NULL;
        gfx_buffer_t *wallpaper = image_load(argv[1], &error);
        if (!wallpaper)
        {
            printf("gfxdemo: %s: %s\n", argv[1], error);
            return 1;
        }
        gfx_blend_scaled(screen, gfx_rect(0, 0, w, h), wallpaper,
                         gfx_rect(0, 0, wallpaper->width, wallpaper->height), 255);
        gfx_buffer_free(wallpaper);
    }
    gfx_fill_rect(screen, gfx_rect(0, h - 40, w, 40), gfx_rgba(10, 10, 20, 170));
    for (int32_t i = 0; i < 5; i++)
        gfx_fill_round_rect(screen, gfx_rect(12 + i * 40, h - 32, 28, 24), 6,
                            gfx_rgba(255, 255, 255, 60 + i * 30));
    if (title_face)
        bragi_draw(screen, title_face, w - 60, h - 15, "12:00", -1, gfx_rgb(240, 240, 245));

    // the windows, twice: the first time makes the glyphs, the second
    // finds them in the cache
    gfx_rect_t text_win = gfx_rect(w / 10, h / 8, w / 2, h / 2);
    gfx_rect_t term_win = gfx_rect(w * 4 / 10, h * 3 / 8, w / 2, h / 2);
    // (the first pass draws into a copy, so the shadows are blended once)
    uint32_t times[2];
    gfx_buffer_t *scratch = gfx_buffer_new(w, h);
    for (int pass = 0; pass < 2; pass++)
    {
        gfx_buffer_t *target = pass == 0 && scratch ? scratch : screen;
        if (target == scratch)
            gfx_copy(scratch, 0, 0, screen, gfx_rect(0, 0, w, h));
        uint32_t start = now_us();
        draw_text_window(target, text_win);
        draw_terminal_window(target, term_win);
        times[pass] = now_us() - start;
    }
    gfx_buffer_free(scratch);

    gfx_present(fb, fix.line_length, w, h, &format, screen, gfx_rect(0, 0, w, h));

    char c;
    while (read(0, &c, 1) == 1 && c != '\n')
        ;
    gfx_buffer_free(screen);
    printf("\033[2J\033[H");
    printf("gfxdemo: windows drawn in %u us, then %u us with the glyph cache\n",
           (unsigned)times[0], (unsigned)times[1]);
    if (body_face)
    {
        bragi_stats_t st;
        bragi_face_stats(body_face, &st);
        printf("gfxdemo: body text: %u glyphs made, %u from the cache, %u bytes\n",
               (unsigned)st.misses, (unsigned)st.hits, (unsigned)st.bytes);
    }
    return 0;
}

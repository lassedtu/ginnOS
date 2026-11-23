/**
 * @file freyja_test.c
 * @brief host tests of Freyja's pictures (halving, turning, drawing) and
 *        of its folder listing.
 */

#include "../folder.h"
#include "../picture.h"
#include "hosttest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t at(const gfx_buffer_t *b, int32_t x, int32_t y)
{
    return b->pixels[(size_t)y * b->stride + x];
}

static void set(gfx_buffer_t *b, int32_t x, int32_t y, uint32_t c)
{
    b->pixels[(size_t)y * b->stride + x] = c;
}

static void test_halve(void)
{
    gfx_buffer_t *b = gfx_buffer_new(4, 2);
    // a 2x2 block of white and black is grey; a block of red stays red
    set(b, 0, 0, 0xFFFFFFFF);
    set(b, 1, 0, 0xFF000000);
    set(b, 0, 1, 0xFF000000);
    set(b, 1, 1, 0xFFFFFFFF);
    for (int y = 0; y < 2; y++)
        for (int x = 2; x < 4; x++)
            set(b, x, y, 0xFFFF0000);
    gfx_buffer_t *h = picture_halve(b);
    CHECK_EQ(h->width, 2);
    CHECK_EQ(h->height, 1);
    CHECK_EQ(at(h, 0, 0), 0xFF808080);
    CHECK_EQ(at(h, 1, 0), 0xFFFF0000);
    gfx_buffer_free(h);
    gfx_buffer_free(b);

    // a 1x1 picture stays 1x1
    b = gfx_buffer_new(1, 1);
    set(b, 0, 0, 0xFF102030);
    h = picture_halve(b);
    CHECK_EQ(h->width, 1);
    CHECK_EQ(at(h, 0, 0), 0xFF102030);
    gfx_buffer_free(h);
    gfx_buffer_free(b);
}

static void test_levels_and_rotate(void)
{
    picture_t p = {0};
    gfx_buffer_t *b = gfx_buffer_new(300, 100);
    set(b, 0, 0, 0xFFFF0000);   // red top left
    set(b, 299, 0, 0xFF00FF00); // green top right
    CHECK(picture_set(&p, b));
    // 300, 150, 75, 37: down to 64 or less on both sides
    CHECK_EQ(p.level_count, 4);
    CHECK_EQ(p.levels[3]->width, 37);

    CHECK(picture_rotate(&p, false));
    CHECK_EQ(picture_width(&p), 100);
    CHECK_EQ(picture_height(&p), 300);
    // a turn to the right: the top left goes to the top right
    CHECK_EQ(at(p.levels[0], 99, 0), 0xFFFF0000);
    CHECK_EQ(at(p.levels[0], 99, 299), 0xFF00FF00);
    CHECK(picture_rotate(&p, true));
    CHECK_EQ(at(p.levels[0], 0, 0), 0xFFFF0000);
    CHECK_EQ(at(p.levels[0], 299, 0), 0xFF00FF00);
    picture_free(&p);
    CHECK_EQ(p.level_count, 0);
}

static void test_draw(void)
{
    picture_t p = {0};
    gfx_buffer_t *b = gfx_buffer_new(2, 2);
    set(b, 0, 0, 0xFFFF0000);
    set(b, 1, 0, 0xFF00FF00);
    set(b, 0, 1, 0x00000000); // see-through: the check shows
    set(b, 1, 1, 0x80000000); // half black over the check
    picture_set(&p, b);
    gfx_buffer_t *dst = gfx_buffer_new(20, 20);
    gfx_fill_rect(dst, gfx_rect(0, 0, 20, 20), 0xFF0000FF);
    // at 4x, at (2, 2): squares of 4 pixels, no blur
    picture_draw(&p, dst, 2, 2, 4.0, 0xFFC0C0C0, 0xFF404040, 100);
    CHECK_EQ(at(dst, 1, 1), 0xFF0000FF); // outside: not drawn
    CHECK_EQ(at(dst, 2, 2), 0xFFFF0000);
    CHECK_EQ(at(dst, 5, 5), 0xFFFF0000);
    CHECK_EQ(at(dst, 6, 2), 0xFF00FF00);
    CHECK_EQ(at(dst, 2, 6), 0xFFC0C0C0);
    uint32_t half = at(dst, 6, 6);
    CHECK_EQ(half >> 24, 0xFF);
    CHECK((half & 0xFF) >= 0x5E && (half & 0xFF) <= 0x62); // C0 halved
    CHECK_EQ(at(dst, 10, 10), 0xFF0000FF);

    // only the clip is drawn
    gfx_fill_rect(dst, gfx_rect(0, 0, 20, 20), 0xFF0000FF);
    dst->clip = gfx_rect(0, 0, 4, 20);
    picture_draw(&p, dst, 2, 2, 4.0, 0xFFC0C0C0, 0xFF404040, 100);
    CHECK_EQ(at(dst, 3, 3), 0xFFFF0000);
    CHECK_EQ(at(dst, 4, 3), 0xFF0000FF);
    gfx_buffer_free(dst);
    picture_free(&p);
}

static void touch(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (f)
        fclose(f);
}

static void test_folder(void)
{
    CHECK(folder_is_picture("a.png"));
    CHECK(folder_is_picture("B.JPG"));
    CHECK(folder_is_picture("c.jpeg"));
    CHECK(!folder_is_picture("notes.txt"));
    CHECK(!folder_is_picture(".png"));
    CHECK(!folder_is_picture("png"));

    char dir[] = "/tmp/freyja-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    touch(dir, "b.png");
    touch(dir, "A.jpg");
    touch(dir, "c.bmp");
    touch(dir, "readme.txt");
    touch(dir, ".hidden.png");

    folder_t f = {0};
    char path[512];
    snprintf(path, sizeof(path), "%s/b.png", dir);
    CHECK(folder_read(&f, path));
    CHECK_EQ(f.count, 3);
    CHECK(strcmp(f.names[0], "A.jpg") == 0);
    CHECK_EQ(f.current, 1);
    char *next = folder_step(&f, 1);
    CHECK(next && strstr(next, "/c.bmp"));
    free(next);
    next = folder_step(&f, 1); // round to the start
    CHECK(next && strstr(next, "/A.jpg"));
    free(next);
    next = folder_step(&f, -1); // and back round to the end
    CHECK(next && strstr(next, "/c.bmp"));
    free(next);
    folder_free(&f);

    const char *names[] = {"b.png", "A.jpg", "c.bmp", "readme.txt", ".hidden.png"};
    for (size_t i = 0; i < 5; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        unlink(path);
    }
    rmdir(dir);

    // a path without a folder cannot be read
    CHECK(!folder_read(&f, "no-slash"));
}

int main(void)
{
    test_halve();
    test_levels_and_rotate();
    test_draw();
    test_folder();
    return hosttest_done("freyja");
}

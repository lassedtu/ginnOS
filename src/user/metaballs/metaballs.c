/**
 * @file metaballs.c
 * @brief framebuffer metaballs demo.
 *
 * several balls drift around and bounce off the screen edges. every pixel sums
 * an integer field contribution from each ball (~ R^2 / distance^2); where the
 * sum crosses a threshold the pixel is drawn, so nearby balls merge into
 * sticky, morphing blobs.
 *
 * default: solid white blobs on black. with "color" as argv[1]: the field
 * strength is mapped to a blue->cyan->green->yellow->red->white ramp, which
 * also exercises the framebuffer's colour packing across the spectrum.
 *
 * runs until 'q' is pressed. usage: metaballs [color]
 */

#include <fb.h>
#include <stdio.h>
#include <string.h>

#define NUM_BALLS 10

typedef struct
{
    int x, y;   // position (pixels, fixed origin)
    int vx, vy; // velocity (pixels per frame)
    int r;      // radius parameter
} ball_t;

static ball_t balls[NUM_BALLS];

// a tiny deterministic PRNG so we don't need a real rand().
static unsigned int rng_state = 0x1234567u;
static unsigned int rng(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) & 0x7FFF;
}

// pack r,g,b into a pixel using the framebuffer's channel masks.
static uint32_t pack(const fb_info_t *fb, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t pr = (uint32_t)(r >> (8 - fb->red_size)) << fb->red_shift;
    uint32_t pg = (uint32_t)(g >> (8 - fb->green_size)) << fb->green_shift;
    uint32_t pb = (uint32_t)(b >> (8 - fb->blue_size)) << fb->blue_shift;
    return pr | pg | pb;
}

// write one pixel into the mapped framebuffer, honoring bpp and pitch.
static void put_pixel(uint8_t *fbmem, const fb_info_t *fb, uint32_t x, uint32_t y, uint32_t px)
{
    uint32_t bpp_bytes = fb->bpp / 8;
    uint32_t off = y * fb->pitch + x * bpp_bytes;
    if (bpp_bytes == 4)
    {
        *(uint32_t *)(fbmem + off) = px;
    }
    else
    {
        fbmem[off + 0] = (uint8_t)(px & 0xFF);
        fbmem[off + 1] = (uint8_t)((px >> 8) & 0xFF);
        fbmem[off + 2] = (uint8_t)((px >> 16) & 0xFF);
    }
}

// map a field strength (0..~threshold*2) to an RGB ramp for colour mode.
static void ramp_color(int f, int threshold, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // scale so 'threshold' sits mid-ramp; clamp to 0..255.
    int t = (f * 128) / (threshold + 1);
    if (t > 255)
        t = 255;
    if (t < 0)
        t = 0;

    // blue -> cyan -> green -> yellow -> red -> white across the range.
    if (t < 64)
    {
        *r = 0;
        *g = (uint8_t)(t * 4);
        *b = 255;
    }
    else if (t < 128)
    {
        *r = 0;
        *g = 255;
        *b = (uint8_t)(255 - (t - 64) * 4);
    }
    else if (t < 192)
    {
        *r = (uint8_t)((t - 128) * 4);
        *g = 255;
        *b = 0;
    }
    else
    {
        *r = 255;
        *g = (uint8_t)(255 - (t - 192) * 4);
        *b = (uint8_t)((t - 192) * 4);
    }
}

// small frame-pacing delay (no sleep syscall yet). kept short: under emulation
// the render is already the bulk of a frame, so a big delay only adds lag.
static void delay(void)
{
    for (volatile unsigned int i = 0; i < 500000u; i++)
    {
    }
}

int main(int argc, char **argv)
{
    fb_info_t fb;
    if (fbinfo(&fb) != 0)
    {
        printf("metaballs: no framebuffer available\n");
        return 1;
    }

    uint8_t *fbmem = (uint8_t *)fbmap();
    if (!fbmem)
    {
        printf("metaballs: framebuffer map failed\n");
        return 1;
    }

    int color_mode = (argc > 1 && strcmp(argv[1], "color") == 0);

    // seed the balls with random positions/velocities inside the screen.
    for (int i = 0; i < NUM_BALLS; i++)
    {
        balls[i].x = (int)(rng() % fb.width);
        balls[i].y = (int)(rng() % fb.height);
        balls[i].vx = (int)(rng() % 7) - 3;
        balls[i].vy = (int)(rng() % 7) - 3;
        if (balls[i].vx == 0)
            balls[i].vx = 2;
        if (balls[i].vy == 0)
            balls[i].vy = 2;
        balls[i].r = 60 + (int)(rng() % 40); // radius parameter
    }

    // field threshold: a pixel is "inside" the blob when the summed field
    // crosses this. tuned against the R^2/dist^2 * 256 scaling below.
    const int threshold = 220;

    // to keep it fast we compute the field on a coarse grid and fill blocks.
    // 4x4 sampling cuts the per-frame field work ~4x versus per-pixel, which
    // matters a lot under i386 emulation; blobs are smooth so it stays clean.
    const int step = 4;

    printf("metaballs: running (%s mode) - press q to quit\n", color_mode ? "color" : "white");

    for (;;)
    {
        // quit on 'q'.
        int key = pollkey();
        if (key == 'q' || key == 'Q')
        {
            // hand the screen back to the console cleanly: clear it and move
            // the cursor home so the shell resumes at the top-left instead of
            // wherever the console cursor was when we launched.
            // built as explicit bytes (ESC = 0x1B) to avoid GCC emitting a
            // .base64 directive for a string literal containing \033.
            char seq[] = {0x1B, '[', '2', 'J', 0x1B, '[', 'H', '\0'};
            for (int i = 0; seq[i]; i++)
            {
                putchar(seq[i]);
            }
            break;
        }

        // advance and bounce the balls.
        for (int i = 0; i < NUM_BALLS; i++)
        {
            balls[i].x += balls[i].vx;
            balls[i].y += balls[i].vy;
            if (balls[i].x < 0)
            {
                balls[i].x = 0;
                balls[i].vx = -balls[i].vx;
            }
            if (balls[i].x >= (int)fb.width)
            {
                balls[i].x = (int)fb.width - 1;
                balls[i].vx = -balls[i].vx;
            }
            if (balls[i].y < 0)
            {
                balls[i].y = 0;
                balls[i].vy = -balls[i].vy;
            }
            if (balls[i].y >= (int)fb.height)
            {
                balls[i].y = (int)fb.height - 1;
                balls[i].vy = -balls[i].vy;
            }
        }

        // render the field.
        for (uint32_t y = 0; y < fb.height; y += step)
        {
            for (uint32_t x = 0; x < fb.width; x += step)
            {
                int field = 0;
                for (int i = 0; i < NUM_BALLS; i++)
                {
                    int dx = (int)x - balls[i].x;
                    int dy = (int)y - balls[i].y;
                    int dist2 = dx * dx + dy * dy + 1;
                    field += (balls[i].r * balls[i].r * 256) / dist2;
                }

                uint32_t px;
                if (color_mode)
                {
                    uint8_t r, g, b;
                    if (field < threshold / 4)
                    {
                        r = g = b = 0; // background stays black
                    }
                    else
                    {
                        ramp_color(field, threshold, &r, &g, &b);
                    }
                    px = pack(&fb, r, g, b);
                }
                else
                {
                    // white where inside the blob, black otherwise.
                    px = (field >= threshold) ? pack(&fb, 255, 255, 255) : pack(&fb, 0, 0, 0);
                }

                // fill the step x step block for this sample.
                for (uint32_t by = 0; by < (uint32_t)step && y + by < fb.height; by++)
                {
                    for (uint32_t bx = 0; bx < (uint32_t)step && x + bx < fb.width; bx++)
                    {
                        put_pixel(fbmem, &fb, x + bx, y + by, px);
                    }
                }
            }
        }

        delay();
    }

    return 0;
}

/**
 * doom: the ginnOS platform layer for doomgeneric.
 *
 * the engine draws each frame into DG_ScreenBuffer (DOOMGENERIC_RESX x
 * DOOMGENERIC_RESY pixels, 0x00RRGGBB). this file copies the frame to the
 * framebuffer (/dev/fb0, mapped with mmap), scaled up by a whole number and
 * centred. the keys come from the console as raw key events (the XKEYRAW
 * termios flag), read without waiting, and become doom key codes by their
 * position on the keyboard, not by the keyboard layout.
 *
 * the console settings are restored when doom ends, also after a fatal
 * signal, so the shell works again.
 */

#include "doomgeneric.h"
#include "doomkeys.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>

// the key event of the console (unistd.h; unistd.h itself is not included,
// because its KEY_ names are the same as doom's)
#define CONSOLE_KEY_EVENT_RAW 2
typedef struct
{
    int type;
    union
    {
        char character;
        int special;
        struct
        {
            unsigned char code;
            unsigned char pressed;
        } raw;
    };
} ginnos_key_event_t;

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int dup(int fd);
int dup2(int old_fd, int new_fd);

// where the game data is: Freedoom (tools/freedoom/fetch.sh) and the
// user's own WADs (the Makefile's DOOM_WAD_DIR). without -iwad, the first
// of these that exists is used: the commercial games, then the shareware
// Doom, then Freedoom (the order of doomgeneric's own search).
static const char *const iwad_choices[] = {
    "/usr/share/doom/doom2.wad",     "/usr/share/doom/plutonia.wad",
    "/usr/share/doom/tnt.wad",       "/usr/share/doom/doom.wad",
    "/usr/share/doom/doom1.wad",     "/usr/share/doom/freedoom2.wad",
    "/usr/share/doom/freedoom1.wad",
};

/**
 * the first game data file that exists, or NULL.
 */
static const char *find_iwad(void)
{
    for (size_t i = 0; i < sizeof(iwad_choices) / sizeof(iwad_choices[0]); i++)
    {
        int fd = open(iwad_choices[i], O_RDONLY);
        if (fd >= 0)
        {
            close(fd);
            return iwad_choices[i];
        }
    }
    return NULL;
}

static uint8_t *screen;      // the mapped framebuffer
static uint32_t screen_pitch; // bytes per framebuffer row
static uint32_t screen_bpp;   // bytes per framebuffer pixel
static fb_var_screeninfo_t screen_info;
static uint32_t scale;        // each doom pixel is scale x scale pixels
static uint32_t origin_x;     // where the doom picture starts on the screen
static uint32_t origin_y;
static uint32_t *row_buffer;  // one scaled row in the framebuffer format

static struct termios saved_settings;
static int settings_saved;
static int console_fd = 1; // the console, after stdout goes to /dev/null

static void restore_console(void)
{
    if (settings_saved)
    {
        tcsetattr(0, TCSANOW, &saved_settings);
        settings_saved = 0;
        // clear the screen, home the cursor and show it again
        const char reset[] = "\033[2J\033[H\033[?25h";
        write(console_fd, reset, sizeof(reset) - 1);
    }
}

static void fatal_signal(int sig)
{
    restore_console();
    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * raw key events, read without waiting, no echo and no signal keys.
 */
static void setup_console(void)
{
    if (tcgetattr(0, &saved_settings) == 0)
    {
        settings_saved = 1;
        atexit(restore_console);
        signal(SIGINT, fatal_signal);
        signal(SIGTERM, fatal_signal);
        signal(SIGSEGV, fatal_signal);
        signal(SIGFPE, fatal_signal);
        signal(SIGILL, fatal_signal);
    }

    struct termios raw = saved_settings;
    raw.c_lflag = XKEYEVENTS | XKEYRAW;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);

    const char hide_cursor[] = "\033[?25l";
    write(1, hide_cursor, sizeof(hide_cursor) - 1);

    // the engine prints messages on stdout while it runs; the console would
    // draw them over the picture. they go to /dev/null; errors still go to
    // stderr, after the console is restored
    int saved = dup(1);
    int null_fd = open("/dev/null", O_WRONLY);
    if (saved >= 0 && null_fd >= 0)
    {
        fflush(stdout);
        dup2(null_fd, 1);
        console_fd = saved;
    }
    if (null_fd >= 0)
    {
        close(null_fd);
    }
}

/**
 * a 0x00RRGGBB pixel in the framebuffer's channel layout.
 */
static uint32_t convert_pixel(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t g = (rgb >> 8) & 0xFF;
    uint32_t b = rgb & 0xFF;
    return ((r >> (8 - screen_info.red.length)) << screen_info.red.offset) |
           ((g >> (8 - screen_info.green.length)) << screen_info.green.offset) |
           ((b >> (8 - screen_info.blue.length)) << screen_info.blue.offset);
}

void DG_Init(void)
{
    int fd = open("/dev/fb0", O_RDWR);
    fb_fix_screeninfo_t fix;
    if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &screen_info) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0)
    {
        fprintf(stderr, "doom: no framebuffer (/dev/fb0)\n");
        exit(1);
    }

    screen_bpp = screen_info.bits_per_pixel / 8;
    screen_pitch = fix.line_length;
    if (screen_bpp != 4 && screen_bpp != 3)
    {
        fprintf(stderr, "doom: %u bits per pixel is not supported\n",
                screen_info.bits_per_pixel);
        exit(1);
    }

    screen = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (screen == MAP_FAILED)
    {
        fprintf(stderr, "doom: cannot map the framebuffer\n");
        exit(1);
    }

    scale = screen_info.xres / DOOMGENERIC_RESX;
    if (screen_info.yres / DOOMGENERIC_RESY < scale)
    {
        scale = screen_info.yres / DOOMGENERIC_RESY;
    }
    if (scale == 0)
    {
        fprintf(stderr, "doom: the screen is smaller than %ux%u\n", DOOMGENERIC_RESX,
                DOOMGENERIC_RESY);
        exit(1);
    }
    origin_x = (screen_info.xres - DOOMGENERIC_RESX * scale) / 2;
    origin_y = (screen_info.yres - DOOMGENERIC_RESY * scale) / 2;

    row_buffer = malloc(DOOMGENERIC_RESX * scale * sizeof(uint32_t));
    if (!row_buffer)
    {
        fprintf(stderr, "doom: out of memory\n");
        exit(1);
    }

    setup_console();

    // black around the picture
    for (uint32_t y = 0; y < screen_info.yres; y++)
    {
        memset(screen + y * screen_pitch, 0, screen_info.xres * screen_bpp);
    }
}

void DG_DrawFrame(void)
{
    uint32_t width = DOOMGENERIC_RESX * scale;
    int direct = screen_bpp == 4 && screen_info.red.offset == 16 &&
                  screen_info.green.offset == 8 && screen_info.blue.offset == 0;

    for (uint32_t y = 0; y < DOOMGENERIC_RESY; y++)
    {
        const uint32_t *src = DG_ScreenBuffer + y * DOOMGENERIC_RESX;

        // one scaled row, then copy it scale times
        uint32_t *out = row_buffer;
        for (uint32_t x = 0; x < DOOMGENERIC_RESX; x++)
        {
            uint32_t pixel = direct ? src[x] : convert_pixel(src[x]);
            for (uint32_t i = 0; i < scale; i++)
            {
                *out++ = pixel;
            }
        }

        for (uint32_t i = 0; i < scale; i++)
        {
            uint8_t *dst = screen + (origin_y + y * scale + i) * screen_pitch +
                           origin_x * screen_bpp;
            if (screen_bpp == 4)
            {
                memcpy(dst, row_buffer, width * 4);
            }
            else
            {
                for (uint32_t x = 0; x < width; x++)
                {
                    dst[x * 3] = (uint8_t)row_buffer[x];
                    dst[x * 3 + 1] = (uint8_t)(row_buffer[x] >> 8);
                    dst[x * 3 + 2] = (uint8_t)(row_buffer[x] >> 16);
                }
            }
        }
    }
}

void DG_SleepMs(uint32_t ms)
{
    struct timespec span = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    nanosleep(&span, NULL);
}

uint32_t DG_GetTicksMs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

// set 1 scan codes (without 0xE0) to doom keys, by the US key positions
static const unsigned char scancode_keys[0x59] = {
    [0x01] = KEY_ESCAPE, [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0', [0x0C] = KEY_MINUS,
    [0x0D] = KEY_EQUALS, [0x0E] = KEY_BACKSPACE, [0x0F] = KEY_TAB, [0x10] = 'q', [0x11] = 'w',
    [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']', [0x1C] = KEY_ENTER,
    [0x1D] = KEY_FIRE, [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x29] = '`', [0x2A] = KEY_RSHIFT, [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c',
    [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.',
    [0x35] = '/', [0x36] = KEY_RSHIFT, [0x37] = '*', [0x38] = KEY_LALT, [0x39] = KEY_USE,
    [0x3A] = KEY_CAPSLOCK, [0x3B] = KEY_F1, [0x3C] = KEY_F2, [0x3D] = KEY_F3, [0x3E] = KEY_F4,
    [0x3F] = KEY_F5, [0x40] = KEY_F6, [0x41] = KEY_F7, [0x42] = KEY_F8, [0x43] = KEY_F9,
    [0x44] = KEY_F10, [0x45] = KEY_NUMLOCK, [0x46] = KEY_SCRLCK, [0x47] = KEY_HOME,
    [0x48] = KEY_UPARROW, [0x49] = KEY_PGUP, [0x4A] = '-', [0x4B] = KEY_LEFTARROW, [0x4C] = '5',
    [0x4D] = KEY_RIGHTARROW, [0x4E] = '+', [0x4F] = KEY_END, [0x50] = KEY_DOWNARROW,
    [0x51] = KEY_PGDN, [0x52] = KEY_INS, [0x53] = KEY_DEL, [0x57] = KEY_F11, [0x58] = KEY_F12,
};

/**
 * the doom key of a raw code (bit 7: an 0xE0 key), or 0.
 */
static unsigned char doom_key(unsigned char code)
{
    unsigned char key = code & 0x7F;
    if (code & 0x80)
    {
        switch (key)
        {
        case 0x1C: return KEY_ENTER;      // keypad enter
        case 0x1D: return KEY_FIRE;       // right ctrl
        case 0x38: return KEY_LALT;       // right alt
        case 0x35: return '/';            // keypad /
        default: break;                   // arrows and the block above them
        }
    }
    return key < sizeof(scancode_keys) ? scancode_keys[key] : 0;
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    ginnos_key_event_t event;
    while (read(0, &event, sizeof(event)) == (ssize_t)sizeof(event))
    {
        if (event.type != CONSOLE_KEY_EVENT_RAW)
        {
            continue;
        }
        unsigned char doom = doom_key(event.raw.code);
        if (doom != 0)
        {
            *pressed = event.raw.pressed;
            *key = doom;
            return 1;
        }
    }
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

int main(int argc, char **argv)
{
    // without -iwad, use the best game data in /usr/share/doom
    int has_iwad = 0;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-iwad") == 0)
        {
            has_iwad = 1;
        }
    }

    char *args[64];
    int count = 0;
    for (int i = 0; i < argc && count < 60; i++)
    {
        args[count++] = argv[i];
    }
    const char *iwad = has_iwad ? NULL : find_iwad();
    if (iwad)
    {
        args[count++] = "-iwad";
        args[count++] = (char *)iwad;
    }
    args[count] = NULL;

    doomgeneric_Create(count, args);
    for (;;)
    {
        doomgeneric_Tick();
    }
}

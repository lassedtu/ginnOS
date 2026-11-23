/**
 * @file doomgeneric_ginnos_window.c
 * @brief Doom in a window of the desktop (heimdalld).
 *
 * when heimdalld runs, Doom draws into a window of its own and takes its
 * keys from the window server; the picture is scaled to the window with
 * the 4:3 shape of the original screens, black around it. without the
 * desktop, doomgeneric_ginnos.c uses the framebuffer as before.
 *
 * this file is apart from doomgeneric_ginnos.c because the Linux key
 * names of <sys/input.h> (through heimdall.h) are the same as Doom's own.
 */

#include "doomgeneric_ginnos_window.h"

#include <heimdall.h>
#include <stdlib.h>
#include <string.h>

#define KEYS_QUEUED 64
#define KEYS_HELD   16

static heim_conn_t *conn;
static heim_window_t *win;
static const uint32_t *last_frame; // for a redraw after a resize
static int frame_width;
static int frame_height;
static bool frame_pending; // a frame came while the server was busy

static struct
{
    uint16_t code;
    bool pressed;
} queue[KEYS_QUEUED];
static int queue_head;
static int queue_count;
static uint16_t held[KEYS_HELD]; // keys down now, to let go when the focus goes

static void push_key(uint16_t code, bool pressed)
{
    if (queue_count == KEYS_QUEUED)
        return;
    int i = (queue_head + queue_count) % KEYS_QUEUED;
    queue[i].code = code;
    queue[i].pressed = pressed;
    queue_count++;
}

static void hold(uint16_t code, bool down)
{
    for (int i = 0; i < KEYS_HELD; i++)
    {
        if (down && held[i] == 0)
        {
            held[i] = code;
            return;
        }
        if (!down && held[i] == code)
            held[i] = 0;
    }
}

/** the keys still down go up: the window lost the keyboard */
static void let_go(void)
{
    for (int i = 0; i < KEYS_HELD; i++)
    {
        if (held[i])
            push_key(held[i], false);
        held[i] = 0;
    }
}

/**
 * the frame scaled into the window: as large as it fits with the 4:3
 * shape (Doom's pixels were taller than wide), black around it.
 */
static void draw(void)
{
    if (!win || !last_frame)
        return;
    if (heim_window_busy(win))
    {
        frame_pending = true; // drawn at HEIM_EV_FRAME
        return;
    }
    frame_pending = false;
    gfx_buffer_t *buf = heim_window_buffer(win);
    int32_t w = buf->width;
    int32_t h = buf->height;
    int32_t pw = w;
    int32_t ph = w * 3 / 4;
    if (ph > h)
    {
        ph = h;
        pw = h * 4 / 3;
    }
    int32_t ox = (w - pw) / 2;
    int32_t oy = (h - ph) / 2;
    static int32_t *columns;
    static int32_t columns_for;
    if (columns_for != pw)
    {
        free(columns);
        columns = malloc((size_t)(pw > 0 ? pw : 1) * sizeof(int32_t));
        columns_for = columns ? pw : 0;
        for (int32_t x = 0; columns && x < pw; x++)
            columns[x] = x * frame_width / pw;
    }
    if (!columns)
        return;
    for (int32_t y = 0; y < h; y++)
    {
        uint32_t *row = buf->pixels + (size_t)y * buf->stride;
        if (y < oy || y >= oy + ph)
        {
            for (int32_t x = 0; x < w; x++)
                row[x] = 0xFF000000u;
            continue;
        }
        const uint32_t *src = last_frame + (size_t)((y - oy) * frame_height / ph) * frame_width;
        for (int32_t x = 0; x < ox; x++)
            row[x] = 0xFF000000u;
        for (int32_t x = 0; x < pw; x++)
            row[ox + x] = 0xFF000000u | (src[columns[x]] & 0xFFFFFF);
        for (int32_t x = ox + pw; x < w; x++)
            row[x] = 0xFF000000u;
    }
    heim_window_commit(win, gfx_rect(0, 0, w, h));
}

bool doom_window_open(int width, int height)
{
    conn = heim_connect();
    if (!conn)
        return false;
    frame_width = width;
    frame_height = height;
    // a whole number of times the 320x240 picture, about 2/3 of the screen
    int32_t sw, sh;
    heim_screen_size(conn, &sw, &sh);
    int32_t scale = 1;
    while (320 * (scale + 1) <= sw * 2 / 3 && 240 * (scale + 1) <= sh * 2 / 3)
        scale++;
    win = heim_window_new(conn, 320 * scale, 240 * scale, "DOOM", HEIM_WINDOW_RESIZABLE);
    if (!win)
    {
        heim_disconnect(conn);
        conn = NULL;
        return false;
    }
    heim_window_set_app(win, "doom");
    return true;
}

void doom_window_frame(const uint32_t *pixels)
{
    last_frame = pixels;
    draw();
}

/** take what the server sent, without waiting */
static void pump(void)
{
    heim_event_t ev;
    int got;
    while ((got = heim_next_event(conn, &ev, 0)) > 0)
    {
        switch (ev.type)
        {
        case HEIM_EV_CLOSE:
            exit(0);
        case HEIM_EV_CONFIGURE:
            if (heim_window_resize(win, ev.msg.configure.width, ev.msg.configure.height))
                draw();
            break;
        case HEIM_EV_FRAME:
            if (frame_pending)
                draw();
            break;
        case HEIM_EV_FOCUS:
            if (!ev.msg.focus.focused)
                let_go();
            break;
        case HEIM_EV_KEY:
            if (ev.msg.key.value == HEIM_KEY_REPEAT)
                break; // Doom knows a key is held
            hold(ev.msg.key.code, ev.msg.key.value == HEIM_KEY_PRESS);
            push_key(ev.msg.key.code, ev.msg.key.value == HEIM_KEY_PRESS);
            break;
        default:
            break;
        }
    }
    if (got < 0)
        exit(0); // the desktop went away
}

bool doom_window_key(int *pressed, unsigned *code)
{
    pump();
    if (queue_count == 0)
        return false;
    *code = queue[queue_head].code;
    *pressed = queue[queue_head].pressed;
    queue_head = (queue_head + 1) % KEYS_QUEUED;
    queue_count--;
    return true;
}

void doom_window_title(const char *title)
{
    if (win)
        heim_window_set_title(win, title);
}

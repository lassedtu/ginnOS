/**
 * @file heimdemo.c
 * @brief a test client of heimdalld: windows that show what they get.
 *
 * usage: heimdemo [windows]
 *
 * each window shows the last key, the text typed into it, the pointer,
 * and a dot where a button was pressed. Ctrl+C puts the text on the
 * clipboard, Ctrl+V adds the clipboard's text, Backspace removes a
 * character. the windows can be resized; the close button closes one,
 * and the program ends with its last window.
 */

#include <bragi.h>
#include <gfx.h>
#include <heimdall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>

#define TEXT_MAX 256
#define DOTS_MAX 64

typedef struct
{
    heim_window_t *win;
    int number;
    char text[TEXT_MAX];
    char last_key[48];
    int32_t px;
    int32_t py;
    bool inside;
    bool focused;
    int32_t dots[DOTS_MAX][3]; // x, y, button
    int dot_count;
    bool dirty; // changed since the last draw
} demo_t;

static bragi_face_t *face;
static bragi_face_t *big;

static void draw(demo_t *d)
{
    gfx_buffer_t *b = heim_window_buffer(d->win);
    int32_t w = b->width;
    int32_t h = b->height;
    gfx_fill_gradient(b, gfx_rect(0, 0, w, h), gfx_rgb(250, 250, 252), gfx_rgb(226, 232, 244));

    if (face)
    {
        char line[96];
        int32_t lh = bragi_line_height(face);
        int32_t y = 16 + bragi_ascent(face);
        snprintf(line, sizeof(line), "window %d, %d x %d%s", d->number, (int)w, (int)h,
                 d->focused ? ", has the keyboard" : "");
        bragi_draw(b, face, 16, y, line, -1, gfx_rgb(30, 32, 40));
        y += lh;
        snprintf(line, sizeof(line), "pointer: %s %d, %d", d->inside ? "in" : "out", (int)d->px,
                 (int)d->py);
        bragi_draw(b, face, 16, y, line, -1, gfx_rgb(90, 94, 110));
        y += lh;
        bragi_draw(b, face, 16, y, d->last_key, -1, gfx_rgb(90, 94, 110));
        y += lh + 8;

        // the text, in a box with a caret
        gfx_rect_t box = gfx_rect(16, y, w - 32, h - y - 16);
        if (box.h > 2 * lh && box.w > 40)
        {
            gfx_fill_round_rect(b, box, 8, gfx_rgb(255, 255, 255));
            gfx_rect_t inner = gfx_rect(box.x + 12, box.y + 10, box.w - 24, box.h - 20);
            const char *text = d->text[0] ? d->text : "Type here. Ctrl+C copies, Ctrl+V pastes.";
            bragi_draw_box(b, big ? big : face, inner, text, BRAGI_ALIGN_LEFT,
                           d->text[0] ? gfx_rgb(20, 20, 26) : gfx_rgb(160, 164, 176));
        }
    }

    for (int i = 0; i < d->dot_count; i++)
    {
        gfx_color_t c = d->dots[i][2] == BTN_LEFT    ? gfx_rgba(60, 110, 230, 200)
                        : d->dots[i][2] == BTN_RIGHT ? gfx_rgba(230, 80, 70, 200)
                                                     : gfx_rgba(60, 180, 100, 200);
        gfx_fill_round_rect(b, gfx_rect(d->dots[i][0] - 6, d->dots[i][1] - 6, 12, 12), 6, c);
    }
    heim_window_commit(d->win, gfx_rect(0, 0, w, h));
    d->dirty = false;
}

static void add_text(demo_t *d, const char *s, size_t n)
{
    size_t len = strlen(d->text);
    if (len + n >= TEXT_MAX)
        n = TEXT_MAX - 1 - len;
    memcpy(d->text + len, s, n);
    d->text[len + n] = '\0';
}

int main(int argc, char **argv)
{
    int count = argc > 1 ? atoi(argv[1]) : 1;
    if (count < 1 || count > 8)
        count = 1;
    heim_conn_t *conn = heim_connect();
    if (!conn)
    {
        printf("heimdemo: no window server (start heimdalld)\n");
        return 1;
    }
    bragi_font_t *font = bragi_font_open("/usr/share/fonts/inter/Inter-Regular.ttf");
    face = font ? bragi_face_new(font, 14.0f) : NULL;
    big = font ? bragi_face_new(font, 20.0f) : NULL;

    demo_t demos[8];
    memset(demos, 0, sizeof(demos));
    int open = 0;
    for (int i = 0; i < count; i++)
    {
        char title[32];
        snprintf(title, sizeof(title), "Heimdemo %d", i + 1);
        demos[i].number = i + 1;
        demos[i].win = heim_window_new(conn, 480, 320, title, HEIM_WINDOW_RESIZABLE);
        if (!demos[i].win)
            continue;
        heim_window_set_data(demos[i].win, &demos[i]);
        strcpy(demos[i].last_key, "no key yet");
        draw(&demos[i]);
        open++;
    }

    heim_event_t ev;
    demo_t *paste_to = NULL;
    while (open > 0 && heim_next_event(conn, &ev, -1) > 0)
    {
        demo_t *d = ev.window ? heim_window_data(ev.window) : NULL;
        switch (ev.type)
        {
        case HEIM_EV_CLOSE:
            heim_window_free(d->win);
            d->win = NULL;
            open--;
            continue;
        case HEIM_EV_CONFIGURE:
            heim_window_resize(d->win, ev.msg.configure.width, ev.msg.configure.height);
            break;
        case HEIM_EV_FOCUS:
            d->focused = ev.msg.focus.focused;
            break;
        case HEIM_EV_POINTER:
        {
            const typeof(ev.msg.pointer) *p = &ev.msg.pointer;
            d->px = p->x;
            d->py = p->y;
            if (p->kind == HEIM_POINTER_ENTER)
                d->inside = true;
            else if (p->kind == HEIM_POINTER_LEAVE)
                d->inside = false;
            else if (p->kind == HEIM_POINTER_PRESS && d->dot_count < DOTS_MAX)
            {
                d->dots[d->dot_count][0] = p->x;
                d->dots[d->dot_count][1] = p->y;
                d->dots[d->dot_count][2] = p->button;
                d->dot_count++;
            }
            break;
        }
        case HEIM_EV_KEY:
        {
            const typeof(ev.msg.key) *k = &ev.msg.key;
            if (k->value == HEIM_KEY_RELEASE)
                continue;
            snprintf(d->last_key, sizeof(d->last_key), "key %u, mods %x, \"%s\"", k->code,
                     (unsigned)k->mods, k->text);
            bool ctrl = k->mods & HEIM_MOD_CTRL;
            if (ctrl && k->code == 46) // C
                heim_clipboard_set(conn, d->text, strlen(d->text));
            else if (ctrl && k->code == 47) // V
            {
                paste_to = d;
                heim_clipboard_request(conn);
            }
            else if (k->code == KEY_BACKSPACE && d->text[0])
            {
                // remove one character (and the continuation bytes before it)
                size_t len = strlen(d->text);
                do
                    len--;
                while (len > 0 && ((unsigned char)d->text[len] & 0xC0) == 0x80);
                d->text[len] = '\0';
            }
            else if (k->text[0])
                add_text(d, k->text, strlen(k->text));
            break;
        }
        case HEIM_EV_FRAME:
            // the last frame is on the screen: draw what changed since
            if (d && d->win && d->dirty)
                draw(d);
            continue;
        case HEIM_EV_CLIPBOARD:
            if (paste_to && paste_to->win && ev.clipboard)
            {
                add_text(paste_to, ev.clipboard, ev.clipboard_length);
                d = paste_to;
            }
            break;
        default:
            continue;
        }
        // draw only when the last frame is on the screen: the server may
        // still be reading the buffer (HEIM_EV_FRAME draws it then)
        if (d && d->win)
        {
            d->dirty = true;
            if (!heim_window_busy(d->win))
                draw(d);
        }
    }
    heim_disconnect(conn);
    return 0;
}

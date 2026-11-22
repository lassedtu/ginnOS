/**
 * @file input.c
 * @brief the keyboard and the pointer: from /dev/input to the windows.
 *
 * the server holds the keyboard (INPUT_IOC_GRAB), so the text console does
 * not get the keys. a key goes to the window with the focus, with the
 * characters it types in the user's layout (KBD_IOC_GET_KEYMAP). the
 * pointer goes to the window under it; a window that got a button press
 * gets the pointer until all buttons are up. the title bar, its buttons
 * and the band around a window are the server's own.
 *
 * the server's keys: Alt+Tab (the next window), Alt+F4 (close), and
 * Ctrl+Alt+Backspace (stop the server).
 */

#include "heimdalld.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DOUBLE_CLICK_MS 400
#define KEY_F4 62
#define KEY_RIGHTMETA 126

static int32_t pending_dx;
static int32_t pending_dy;
static bool pending_abs;
static int32_t abs_x = -1;
static int32_t abs_y = -1;

bool input_init(void)
{
    S.input_fd = open("/dev/input", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (S.input_fd < 0)
        return false;
    int on = 1;
    ioctl(S.input_fd, INPUT_IOC_GRAB, &on);

    int kbd = open("/dev/kbd", O_RDONLY | O_CLOEXEC);
    if (kbd >= 0)
    {
        S.have_keymap = ioctl(kbd, KBD_IOC_GET_KEYMAP, &S.keymap) == 0;
        struct kbd_layout_name name = {0};
        if (ioctl(kbd, KBD_IOC_GET_LAYOUT, &name.index) == 0 &&
            ioctl(kbd, KBD_IOC_LAYOUT_NAME, &name) == 0)
            keytext_init(name.name);
        close(kbd);
    }
    return true;
}

static void send_pointer(window_t *w, uint16_t kind, uint16_t button, int32_t value)
{
    gfx_rect_t c = window_content(w);
    heim_msg_t msg = {0};
    msg.type = HEIM_EV_POINTER;
    msg.pointer.kind = kind;
    msg.pointer.button = button;
    msg.pointer.x = S.px - c.x;
    msg.pointer.y = S.py - c.y;
    msg.pointer.value = value;
    msg.pointer.buttons = S.buttons;
    msg.pointer.mods = S.mods;
    client_send_window(w, &msg);
}

static uint32_t edge_cursor(uint32_t edges)
{
    bool h = edges & (EDGE_LEFT | EDGE_RIGHT);
    bool v = edges & (EDGE_TOP | EDGE_BOTTOM);
    if (h && v)
    {
        bool nwse = (edges & EDGE_LEFT) ? (edges & EDGE_TOP) : (edges & EDGE_BOTTOM);
        return nwse ? CURSOR_RESIZE_NWSE : CURSOR_RESIZE_NESW;
    }
    return h ? CURSOR_RESIZE_H : CURSOR_RESIZE_V;
}

static void set_cursor(uint32_t shape)
{
    S.cursor = shape;
}

/**
 * the pointer's window, shape and hover, for where it is now (after it
 * moved, or a window changed under it).
 */
void input_pointer_update(void)
{
    if (S.drag == DRAG_MOVE)
        return;
    if (S.drag == DRAG_RESIZE)
    {
        set_cursor(edge_cursor(S.drag_edges));
        return;
    }

    part_t part;
    uint32_t edges;
    window_t *w = window_at(S.px, S.py, &part, &edges);
    if (S.grab)
    {
        // the grabbing window keeps the pointer
        w = S.grab;
        part = PART_CONTENT;
    }

    // the buttons of the title bars light up under the pointer
    // (while a button is held down, only that button, and only while the
    // pointer is on it)
    for (window_t *o = S.bottom; o; o = o->above)
    {
        part_t hover = PART_NONE;
        if (o == w && (part == PART_MINIMIZE || part == PART_MAXIMIZE || part == PART_CLOSE))
            hover = part;
        if (S.drag == DRAG_BUTTON)
        {
            bool on_it = o == S.drag_window && hover == S.drag_part;
            window_set_hover(o, on_it ? hover : PART_NONE, on_it ? hover : PART_NONE);
        }
        else
            window_set_hover(o, hover, PART_NONE);
    }

    window_t *inside = part == PART_CONTENT ? w : NULL;
    if (inside != S.pointer_window)
    {
        if (S.pointer_window)
            send_pointer(S.pointer_window, HEIM_POINTER_LEAVE, 0, 0);
        S.pointer_window = inside;
        if (inside)
            send_pointer(inside, HEIM_POINTER_ENTER, 0, 0);
    }

    if (part == PART_CONTENT)
        set_cursor(w->cursor);
    else if (part == PART_EDGE)
        set_cursor(edge_cursor(edges));
    else
        set_cursor(HEIM_CURSOR_ARROW);
}

static void pointer_moved(void)
{
    if (S.px < 0)
        S.px = 0;
    if (S.py < 0)
        S.py = 0;
    if (S.px >= S.width)
        S.px = S.width - 1;
    if (S.py >= S.height)
        S.py = S.height - 1;

    if (S.drag == DRAG_MOVE)
    {
        window_move(S.drag_window, S.px - S.drag_dx, S.py - S.drag_dy);
        return;
    }
    if (S.drag == DRAG_RESIZE)
    {
        window_t *w = S.drag_window;
        gfx_rect_t f = window_frame(w);
        int32_t left = f.x;
        int32_t top = f.y;
        int32_t right = f.x + f.w;
        int32_t bottom = f.y + f.h;
        int32_t min_h = WINDOW_MIN_HEIGHT + TITLE_HEIGHT;
        if (S.drag_edges & EDGE_LEFT)
            left = S.px < right - WINDOW_MIN_WIDTH ? S.px : right - WINDOW_MIN_WIDTH;
        if (S.drag_edges & EDGE_RIGHT)
            right = S.px > left + WINDOW_MIN_WIDTH ? S.px : left + WINDOW_MIN_WIDTH;
        if (S.drag_edges & EDGE_TOP)
            top = S.py < bottom - min_h ? S.py : bottom - min_h;
        if (S.drag_edges & EDGE_BOTTOM)
            bottom = S.py > top + min_h ? S.py : top + min_h;
        screen_damage(S.drag_rect);
        S.drag_rect = gfx_rect(left, top, right - left, bottom - top);
        screen_damage(S.drag_rect);
        return;
    }

    if (S.grab)
        send_pointer(S.grab, HEIM_POINTER_MOTION, 0, 0);
    input_pointer_update();
    if (!S.grab && S.pointer_window)
        send_pointer(S.pointer_window, HEIM_POINTER_MOTION, 0, 0);
}

static uint32_t button_bit(uint16_t code)
{
    if (code == BTN_LEFT)
        return HEIM_BUTTON_LEFT;
    if (code == BTN_RIGHT)
        return HEIM_BUTTON_RIGHT;
    return HEIM_BUTTON_MIDDLE;
}

static void button_press(uint16_t code, uint32_t time_ms)
{
    S.buttons |= button_bit(code);
    if (S.grab)
    {
        send_pointer(S.grab, HEIM_POINTER_PRESS, code, 0);
        return;
    }
    if (S.drag != DRAG_NONE)
        return;

    part_t part;
    uint32_t edges;
    window_t *w = window_at(S.px, S.py, &part, &edges);
    // a press outside the open menus closes them, and does nothing else
    if ((!w || !w->parent) && window_close_popups())
        return;
    if (!w)
        return;
    window_raise(w);
    window_focus(w);

    switch (part)
    {
    case PART_CONTENT:
        S.grab = w;
        send_pointer(w, HEIM_POINTER_PRESS, code, 0);
        break;
    case PART_TITLE:
        if (code != BTN_LEFT)
            break;
        if (S.last_click_window == w && time_ms - S.last_click_ms < DOUBLE_CLICK_MS)
        {
            window_toggle_maximize(w);
            S.last_click_window = NULL;
            break;
        }
        S.last_click_window = w;
        S.last_click_ms = time_ms;
        if (!w->maximized)
        {
            S.drag = DRAG_MOVE;
            S.drag_window = w;
            S.drag_dx = S.px - w->x;
            S.drag_dy = S.py - w->y;
        }
        break;
    case PART_MINIMIZE:
    case PART_MAXIMIZE:
    case PART_CLOSE:
        if (code != BTN_LEFT)
            break;
        S.drag = DRAG_BUTTON;
        S.drag_window = w;
        S.drag_part = part;
        window_set_hover(w, part, part);
        break;
    case PART_EDGE:
        if (code != BTN_LEFT)
            break;
        S.drag = DRAG_RESIZE;
        S.drag_window = w;
        S.drag_edges = edges;
        S.drag_rect = window_frame(w);
        screen_damage(S.drag_rect);
        break;
    default:
        break;
    }
    input_pointer_update();
}

static void button_release(uint16_t code)
{
    S.buttons &= ~button_bit(code);
    if (S.grab)
    {
        window_t *w = S.grab;
        send_pointer(w, HEIM_POINTER_RELEASE, code, 0);
        if (S.buttons == 0)
        {
            S.grab = NULL;
            input_pointer_update();
        }
        return;
    }
    if (code != BTN_LEFT)
        return;

    window_t *w = S.drag_window;
    drag_t drag = S.drag;
    S.drag = DRAG_NONE;
    S.drag_window = NULL;
    if (drag == DRAG_BUTTON && w)
    {
        part_t part;
        uint32_t edges;
        window_t *under = window_at(S.px, S.py, &part, &edges);
        window_set_hover(w, PART_NONE, PART_NONE);
        if (under == w && part == S.drag_part)
        {
            if (part == PART_CLOSE)
            {
                heim_msg_t msg = {0};
                msg.type = HEIM_EV_CLOSE;
                client_send_window(w, &msg);
            }
            else if (part == PART_MAXIMIZE)
                window_toggle_maximize(w);
            else if (part == PART_MINIMIZE)
                window_minimize(w);
        }
    }
    else if (drag == DRAG_RESIZE && w)
    {
        screen_damage(S.drag_rect);
        gfx_rect_t r = S.drag_rect;
        window_damage(w);
        w->x = r.x;
        w->y = r.y;
        window_damage(w);
        window_configure(w, r.w, r.h - TITLE_HEIGHT);
    }
    input_pointer_update();
}

static void scroll(int32_t steps)
{
    window_t *w = S.grab ? S.grab : S.pointer_window;
    if (w)
        send_pointer(w, HEIM_POINTER_SCROLL, 0, steps);
}

static uint32_t mod_of(uint16_t code)
{
    switch (code)
    {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
        return HEIM_MOD_SHIFT;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
        return HEIM_MOD_CTRL;
    case KEY_LEFTALT:
        return HEIM_MOD_ALT;
    case KEY_RIGHTALT:
        return HEIM_MOD_ALTGR;
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA:
        return HEIM_MOD_META;
    default:
        return 0;
    }
}

/**
 * the server's own keys.
 * @return true if the key was one of them.
 */
static bool shortcut(uint16_t code)
{
    bool ctrl = S.mods & HEIM_MOD_CTRL;
    bool alt = S.mods & (HEIM_MOD_ALT | HEIM_MOD_ALTGR);
    if (ctrl && alt && code == KEY_BACKSPACE)
    {
        S.quit = true;
        return true;
    }
    if (alt && !ctrl && code == KEY_TAB)
    {
        window_cycle();
        return true;
    }
    if (alt && !ctrl && code == KEY_F4)
    {
        if (S.focus)
        {
            heim_msg_t msg = {0};
            msg.type = HEIM_EV_CLOSE;
            client_send_window(S.focus, &msg);
        }
        return true;
    }
    return false;
}

static void key(uint16_t code, int32_t value)
{
    uint32_t mod = mod_of(code);
    if (mod)
    {
        if (value)
            S.mods |= mod;
        else
            S.mods &= ~mod;
    }
    if (value && !mod && shortcut(code))
        return;
    if (!S.focus)
        return;

    heim_msg_t msg = {0};
    msg.type = HEIM_EV_KEY;
    msg.key.code = code;
    msg.key.value = (uint16_t)value;
    msg.key.mods = S.mods;
    if (value)
        keytext_of(code, S.mods, msg.key.text);
    client_send_window(S.focus, &msg);
}

static void event(const input_event_t *e)
{
    switch (e->type)
    {
    case EV_KEY:
        if (e->code == BTN_LEFT || e->code == BTN_RIGHT || e->code == BTN_MIDDLE)
        {
            if (e->value == 1)
                button_press(e->code, e->time_ms);
            else if (e->value == 0)
                button_release(e->code);
        }
        else
            key(e->code, e->value);
        break;
    case EV_REL:
        if (e->code == REL_X)
            pending_dx += e->value;
        else if (e->code == REL_Y)
            pending_dy += e->value;
        else if (e->code == REL_WHEEL)
            scroll(e->value);
        break;
    case EV_ABS:
        if (e->code == ABS_X)
            abs_x = e->value;
        else if (e->code == ABS_Y)
            abs_y = e->value;
        pending_abs = true;
        break;
    case EV_SYN:
        if (pending_abs && abs_x >= 0 && abs_y >= 0)
        {
            S.px = (int32_t)((int64_t)abs_x * (S.width - 1) / INPUT_ABS_MAX);
            S.py = (int32_t)((int64_t)abs_y * (S.height - 1) / INPUT_ABS_MAX);
            pointer_moved();
        }
        else if (pending_dx || pending_dy)
        {
            S.px += pending_dx;
            S.py += pending_dy;
            pointer_moved();
        }
        pending_dx = 0;
        pending_dy = 0;
        pending_abs = false;
        break;
    default:
        break;
    }
}

void input_read(void)
{
    input_event_t events[64];
    for (;;)
    {
        ssize_t n = read(S.input_fd, events, sizeof(events));
        if (n <= 0)
            return;
        for (ssize_t i = 0; i < n / (ssize_t)sizeof(input_event_t); i++)
            event(&events[i]);
    }
}

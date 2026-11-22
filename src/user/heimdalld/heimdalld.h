#pragma once

/**
 * @file heimdalld.h
 * @brief heimdalld, the window server: what its parts share.
 *
 * the server owns the screen. clients draw their windows into shared
 * memory; the server puts the windows together with the wallpaper, the
 * title bars, the shadows and the pointer into one picture (the
 * composition) and copies the changed parts to the framebuffer. it reads
 * /dev/input and sends the keys to the window with the focus and the
 * pointer events to the window under the pointer.
 *
 * the parts: main.c (the loop), screen.c (composition), window.c (windows
 * and their decorations), client.c (the protocol), input.c (keys and the
 * pointer), cursor.c (the pointer shapes).
 */

#include <bragi.h>
#include <gfx.h>
#include <heimdall_proto.h>
#include <icon.h>
#include <idunn_theme.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/kbd.h>

// sizes of the decorations at the theme's scale (S.theme)
#define TITLE_HEIGHT idunn_px(&S.theme, 36)
#define RESIZE_BAND idunn_px(&S.theme, 6) // the band outside a window that resizes it
#define BUTTON_WIDTH idunn_px(&S.theme, 28)
#define BUTTON_HEIGHT idunn_px(&S.theme, 24)
#define WINDOW_MIN_WIDTH idunn_px(&S.theme, 160)
#define WINDOW_MIN_HEIGHT idunn_px(&S.theme, 80)

#define CLIENT_IN (64 * 16)
#define CLIENT_FDS 8
#define CLIENT_OUT 256
#define DAMAGE_MAX 32

// the parts of a window under the pointer
typedef enum
{
    PART_NONE,
    PART_CONTENT,
    PART_TITLE,
    PART_MINIMIZE,
    PART_MAXIMIZE,
    PART_CLOSE,
    PART_EDGE,
} part_t;

// the edges a resize moves
#define EDGE_LEFT   0x01
#define EDGE_RIGHT  0x02
#define EDGE_TOP    0x04
#define EDGE_BOTTOM 0x08

// the pointer shapes: the protocol's, and the server's own
#define CURSOR_RESIZE_H    3 // left and right
#define CURSOR_RESIZE_V    4 // up and down
#define CURSOR_RESIZE_NWSE 5 // top left and bottom right
#define CURSOR_RESIZE_NESW 6 // top right and bottom left
#define CURSOR_COUNT       7

typedef struct client client_t;

typedef struct window
{
    struct window *below; // the z order: S.bottom up to S.top
    struct window *above;
    client_t *client;
    uint32_t id; // the client's number for it
    uint32_t flags;
    struct window *parent; // a popup's window (NULL: a normal window)
    gfx_rect_t anchor;     // a popup: the rect of the parent's content it goes next to
    uint32_t side;         // HEIM_SIDE_
    int32_t margin;        // the shadow around its card
    char title[HEIM_TITLE_MAX];

    int32_t x; // the top left of the frame (the title bar)
    int32_t y;
    // the client's buffer is copied at each commit (the part it changed)
    // into the server's own copy, and only the copy is drawn: so what the
    // client draws next never shows half done
    gfx_buffer_t content; // the server's copy (the pixels of own)
    gfx_buffer_t *own;
    gfx_buffer_t shared; // over the client's shared memory (read only)
    void *map;
    size_t map_size;
    gfx_buffer_t pending; // attached, shown at the next commit
    void *pending_map;
    size_t pending_size;

    bool shown; // committed at least once
    bool minimized;
    bool maximized;
    gfx_rect_t restore; // the frame before maximizing
    uint32_t cursor;    // HEIM_CURSOR_ over the content

    gfx_buffer_t *bar; // the title bar, drawn when it changes
    bool bar_dirty;
    part_t hover;   // the button under the pointer
    part_t pressed; // the button held down
    bool frame_pending;
} window_t;

struct client
{
    client_t *next;
    int fd;
    bool hello;
    bool dead;
    uint8_t in[CLIENT_IN];
    int in_used;
    int fds[CLIENT_FDS];
    int fd_count;
    heim_msg_t out[CLIENT_OUT];
    int out_fd[CLIENT_OUT];
    int out_head;
    int out_count;
    size_t out_done; // bytes of the first message already sent
};

typedef enum
{
    DRAG_NONE,
    DRAG_MOVE,
    DRAG_RESIZE,
    DRAG_BUTTON,
} drag_t;

typedef struct
{
    // the screen
    int fb_fd;
    int display_fd; // /dev/display, -1 without a display driver
    void *fb;
    size_t fb_size;
    uint32_t pitch;
    gfx_format_t format;
    int32_t width;
    int32_t height;
    gfx_buffer_t *screen;    // the composition
    gfx_buffer_t *wallpaper; // screen size
    const char *wallpaper_path;
    gfx_rect_t damage[DAMAGE_MAX];
    int damage_count;

    // statistics (-s): sent to /dev/kmsg every 2 seconds
    struct
    {
        int fd; // 0: off
        uint32_t since; // ms
        uint32_t frames;
        uint64_t pixels;
        uint64_t compose_us;
        uint64_t present_us;
        uint64_t flush_us;
    } stats;

    // windows and clients
    window_t *bottom;
    window_t *top;
    window_t *focus;
    client_t *clients;
    uint32_t placed; // windows placed so far (for the cascade)

    // the pointer
    int32_t px;
    int32_t py;
    uint32_t buttons; // HEIM_BUTTON_
    uint32_t mods;    // HEIM_MOD_
    window_t *pointer_window; // its content has the pointer
    window_t *grab;           // got the press; gets the pointer until all buttons are up
    uint32_t cursor;
    gfx_rect_t cursor_rect; // where the pointer is drawn
    bool hw_cursor;         // the card shows the pointer: not in the picture

    drag_t drag;
    window_t *drag_window;
    part_t drag_part; // DRAG_BUTTON: the button pressed
    int32_t drag_dx; // from the pointer to the frame's top left
    int32_t drag_dy;
    uint32_t drag_edges;
    gfx_rect_t drag_rect; // the outline of a resize
    uint32_t last_click_ms;
    window_t *last_click_window;

    // the look
    idunn_theme_t theme; // the colours, the radius and the shadows of the decorations
    bragi_font_t *title_font;
    bragi_face_t *title_face;
    icon_t *icon_minimize;
    icon_t *icon_maximize;
    icon_t *icon_restore;
    icon_t *icon_close;

    // the keyboard
    struct kbd_keymap keymap;
    bool have_keymap;

    char *clip;
    uint32_t clip_length;

    int listen_fd;
    int input_fd;
    bool quit;
} server_t;

extern server_t S;

// in screen.c

bool screen_init(const char *wallpaper);
/** set the mode to the size the screen wants (the QEMU window), if it is not */
void screen_follow(void);
/** read /dev/display: follow the size the screen wants, map a new mode */
void screen_display_events(void);
void screen_damage(gfx_rect_t r);
void screen_compose(void);

// in window.c

/** make the shadow, the corners and the title bars from S.theme (again) */
void window_look(void);
/** the screen has a new size: maximized windows take it, the rest stay on it */
void window_screen_changed(void);
window_t *window_new(client_t *client, uint32_t id, uint32_t flags, const char *title,
                     window_t *parent);
bool window_is_popup(const window_t *w);
/** a popup's window at the bottom of its chain (a normal window: itself) */
window_t *window_root(window_t *w);
/** put a popup next to its anchor, after a HEIM_REQ_PLACE */
void window_place(window_t *w);
/**
 * send HEIM_EV_CLOSE to every popup and hide them.
 * @return true if a popup that takes input was open (not only a tooltip).
 */
bool window_close_popups(void);
void window_destroy(window_t *w);
window_t *window_find(client_t *client, uint32_t id);
bool window_decorated(const window_t *w);
gfx_rect_t window_frame(const window_t *w);  // the title bar and the content
gfx_rect_t window_bounds(const window_t *w); // with the shadow and the resize band
gfx_rect_t window_content(const window_t *w);
bool window_visible(const window_t *w);
void window_damage(window_t *w);
void window_draw(window_t *w, gfx_rect_t clip);
void window_show(window_t *w);
void window_raise(window_t *w);
void window_focus(window_t *w);
void window_focus_top(void);
void window_set_hover(window_t *w, part_t hover, part_t pressed);
void window_move(window_t *w, int32_t x, int32_t y);
void window_toggle_maximize(window_t *w);
void window_minimize(window_t *w);
void window_configure(window_t *w, int32_t width, int32_t height);
window_t *window_at(int32_t x, int32_t y, part_t *part, uint32_t *edges);
void window_cycle(void);

// in client.c

void client_accept(int listen_fd);
void client_read(client_t *c);
void client_flush(client_t *c);
void client_send(client_t *c, const heim_msg_t *msg, int fd);
void client_send_window(window_t *w, heim_msg_t *msg);
/** tell every client the new size of the screen */
void client_screen_changed(void);
void client_reap(void);

// in input.c

bool input_init(void);
void input_read(void);
void input_pointer_update(void);

// in keytext.c
void keytext_init(const char *layout_name);
/** the text a key press types with these modifiers, UTF-8, in out (8 bytes) */
void keytext_of(uint16_t code, uint32_t mods, char out[8]);

// in cursor.c

/** make the pointer shapes at a scale (1 or 2) */
void cursor_init(int32_t scale);
gfx_rect_t cursor_rect(uint32_t shape, int32_t x, int32_t y);
void cursor_draw(gfx_buffer_t *buf, uint32_t shape, int32_t x, int32_t y);

/**
 * show the pointer with the card's own cursor (FBIO_CURSOR): moving it
 * then draws nothing. the image goes up again when the shape changes or
 * after cursor_init, else only the place.
 * @return false if the card has no cursor or the shape is too large.
 */
bool cursor_hardware(int fb_fd, uint32_t shape, int32_t x, int32_t y);

/** the card's cursor needs its image again (a new mode) */
void cursor_hardware_reset(void);

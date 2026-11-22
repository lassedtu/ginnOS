#pragma once

/**
 * @file heimdall_proto.h
 * @brief the messages between heimdalld (the window server) and its clients.
 *
 * a client connects to the Unix socket HEIM_SOCKET. every message, in both
 * directions, is one heim_msg_t of 64 bytes. a message that carries an open
 * file (a shared memory buffer) has it in an SCM_RIGHTS control message
 * sent with it.
 *
 * the start: the client sends HEIM_REQ_HELLO, the server answers with
 * HEIM_EV_WELCOME (the screen size).
 *
 * a window: the client picks a window number (not 0, its own), sends
 * HEIM_REQ_CREATE, then HEIM_REQ_ATTACH with a shared memory buffer of
 * premultiplied ARGB pixels, draws into it, and sends HEIM_REQ_COMMIT with
 * the part it changed. the window shows after its first commit. after a
 * commit, the server sends HEIM_EV_FRAME when the change is on the
 * screen; a client that draws again before that can be seen half drawn.
 *
 * the server draws the title bar and the buttons (server-side
 * decorations); the size of a window is the size of its content. a window
 * is placed by the server. HEIM_EV_CONFIGURE asks the client for another
 * size (maximize, a resize with the mouse): the client attaches a buffer
 * of that size and commits it.
 *
 * a popup (a menu, a select's choices, a popover, a tooltip) is a window
 * with HEIM_WINDOW_POPUP and a parent window. it has no decorations and
 * its pixels can be see-through; the client draws its shadow in a margin
 * around it. HEIM_REQ_PLACE gives a rect in the parent's content to show
 * it next to and a side; the server puts it there, on the other side
 * when there is no room, and inside the screen. a popup is above its
 * parent and gets the keyboard (a tooltip, HEIM_WINDOW_TOOLTIP, gets no
 * input at all). a press outside the open popups closes them: each gets
 * HEIM_EV_CLOSE, and the press goes nowhere else.
 */

#include <stdint.h>

#define HEIM_SOCKET "/run/heimdall"
#define HEIM_PROTOCOL_VERSION 1
#define HEIM_TITLE_MAX 40

// the longest settings name (libconf: /etc/ginnos/<name>.conf), with the 0
#define HEIM_SETTINGS_MAX 48
#define HEIM_SIZE_MAX 8192
#define HEIM_CLIPBOARD_MAX (1024 * 1024)

#define HEIM_REQ_HELLO    1 // hello: the protocol version
#define HEIM_REQ_CREATE   2 // create: size, flags, title
#define HEIM_REQ_ATTACH   3 // attach, with the buffer's fd: size, stride
#define HEIM_REQ_COMMIT   4 // rect: the part that changed
#define HEIM_REQ_TITLE    5 // title
#define HEIM_REQ_CURSOR   6 // cursor: the pointer shape over the content
#define HEIM_REQ_DESTROY  7
#define HEIM_REQ_CLIP_SET 8 // clip: the length; with an fd of the text when not 0
#define HEIM_REQ_CLIP_GET 9 // answered with HEIM_EV_CLIPBOARD
#define HEIM_REQ_SETTINGS 10 // settings: a settings file changed; all clients are told
#define HEIM_REQ_PLACE    11 // place: where a popup goes

#define HEIM_EV_WELCOME   64 // size: the screen
#define HEIM_EV_CONFIGURE 65 // configure: the size the window should get
#define HEIM_EV_CLOSE     66 // the user closed the window (the client decides)
#define HEIM_EV_FOCUS     67 // focus: the window got or lost the keyboard
#define HEIM_EV_KEY       68 // key
#define HEIM_EV_POINTER   69 // pointer
#define HEIM_EV_FRAME     70 // the last commit is on the screen
#define HEIM_EV_CLIPBOARD 71 // clip: the length; with an fd of the text when not 0
#define HEIM_EV_SETTINGS  72 // settings: the settings file that changed (read it again)
#define HEIM_EV_SCREEN    73 // size: the screen has a new size

// create flags
#define HEIM_WINDOW_RESIZABLE   0x01 // the user can resize it
#define HEIM_WINDOW_ALPHA       0x02 // the content has see-through pixels
#define HEIM_WINDOW_UNDECORATED 0x04 // no title bar and no shadow
#define HEIM_WINDOW_POPUP       0x08 // a popup of create.parent (see above)
#define HEIM_WINDOW_TOOLTIP     0x10 // with POPUP: no keyboard, no pointer

// the side of a rect a popup goes to (place.side)
#define HEIM_SIDE_BELOW 0 // under it, from its left edge (or above it)
#define HEIM_SIDE_ABOVE 1 // over it, in the middle (or below it)
#define HEIM_SIDE_RIGHT 2 // to its right, from its top (or to its left): a sub-menu

// configure state
#define HEIM_STATE_MAXIMIZED 0x01

// pointer shapes
#define HEIM_CURSOR_ARROW 0
#define HEIM_CURSOR_TEXT  1
#define HEIM_CURSOR_HAND  2

// pointer kinds
#define HEIM_POINTER_MOTION  1
#define HEIM_POINTER_PRESS   2 // button: BTN_LEFT, BTN_RIGHT or BTN_MIDDLE
#define HEIM_POINTER_RELEASE 3
#define HEIM_POINTER_ENTER   4
#define HEIM_POINTER_LEAVE   5
#define HEIM_POINTER_SCROLL  6 // value: wheel steps, positive is up

// buttons held (pointer.buttons)
#define HEIM_BUTTON_LEFT   0x01
#define HEIM_BUTTON_RIGHT  0x02
#define HEIM_BUTTON_MIDDLE 0x04

// modifier keys held (key.mods, pointer.mods)
#define HEIM_MOD_SHIFT 0x01
#define HEIM_MOD_CTRL  0x02
#define HEIM_MOD_ALT   0x04 // the left Alt (Option) key
#define HEIM_MOD_ALTGR 0x08 // the right Alt (Option) key
#define HEIM_MOD_META  0x10

// key values (key.value), as /dev/input
#define HEIM_KEY_RELEASE 0
#define HEIM_KEY_PRESS   1
#define HEIM_KEY_REPEAT  2

typedef struct
{
    uint16_t type;   // HEIM_REQ_ or HEIM_EV_
    uint16_t pad;    // 0
    uint32_t window; // the client's window number (0: not about a window)
    union
    {
        uint8_t raw[56];
        struct
        {
            uint32_t version;
        } hello;
        struct
        {
            int32_t width;
            int32_t height;
        } size;
        struct
        {
            int32_t width;
            int32_t height;
            uint32_t flags; // HEIM_WINDOW_
            char title[HEIM_TITLE_MAX];
            uint32_t parent; // POPUP: the window it belongs to
        } create;
        struct
        {
            int32_t x; // a rect in the parent's content
            int32_t y;
            int32_t width;
            int32_t height;
            uint32_t side;  // HEIM_SIDE_
            int32_t margin; // the shadow around the popup's card in its buffer
        } place;
        struct
        {
            int32_t width;
            int32_t height;
            int32_t stride; // pixels from one row to the next
        } attach;
        struct
        {
            int32_t x;
            int32_t y;
            int32_t width;
            int32_t height;
        } rect;
        struct
        {
            char title[HEIM_TITLE_MAX];
        } title;
        struct
        {
            uint32_t shape; // HEIM_CURSOR_
        } cursor;
        struct
        {
            uint32_t length; // bytes of UTF-8 text
        } clip;
        struct
        {
            char name[HEIM_SETTINGS_MAX]; // "theme"
        } settings;
        struct
        {
            int32_t width;
            int32_t height;
            uint32_t state; // HEIM_STATE_
        } configure;
        struct
        {
            uint32_t focused;
        } focus;
        struct
        {
            uint16_t code;  // the Linux key code (KEY_ in sys/input.h)
            uint16_t value; // HEIM_KEY_
            uint32_t mods;  // HEIM_MOD_
            char text[8];   // the characters it types (UTF-8), or empty
        } key;
        struct
        {
            uint16_t kind;    // HEIM_POINTER_
            uint16_t button;  // PRESS, RELEASE: BTN_
            int32_t x;        // in the content, from its top left
            int32_t y;
            int32_t value;    // SCROLL: steps
            uint32_t buttons; // HEIM_BUTTON_, held after this event
            uint32_t mods;    // HEIM_MOD_
        } pointer;
    };
} heim_msg_t;

_Static_assert(sizeof(heim_msg_t) == 64, "a heimdall message is 64 bytes");

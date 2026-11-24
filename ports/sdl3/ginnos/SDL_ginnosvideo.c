/**
 * @file SDL_ginnosvideo.c
 * @brief SDL's video driver for ginnOS.
 *
 * each SDL window is a heimdall window. SDL draws into the window's
 * buffer itself (the software renderer, or the window surface): the
 * framebuffer that SDL gets is the heimdall buffer, so an update is only
 * a commit. the events of the connection become SDL's keyboard, mouse
 * and window events. the key codes are Linux's (sys/input.h), so SDL's
 * table for Linux gives the scancodes.
 *
 * this file is not SDL's: ports/sdl3/build.sh copies it into SDL's tree
 * as src/video/ginnos/.
 */

#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_GINNOS

#include "SDL_ginnosvideo.h"

#include "../../events/SDL_events_c.h"
#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_windowevents_c.h"
#include "../../events/scancodes_linux.h"

#include <sys/input.h>

// how long GetClipboardText waits for the server's answer
#define CLIPBOARD_WAIT_MS 500

static SDL_Window *sdl_window_of(heim_window_t *heim)
{
    return heim ? (SDL_Window *)heim_window_data(heim) : NULL;
}

static SDL_Scancode scancode_of(uint16_t code)
{
    if (code < SDL_arraysize(linux_scancode_table))
    {
        return linux_scancode_table[code];
    }
    return SDL_SCANCODE_UNKNOWN;
}

static Uint8 button_of(uint16_t button)
{
    switch (button)
    {
    case BTN_LEFT:
        return SDL_BUTTON_LEFT;
    case BTN_RIGHT:
        return SDL_BUTTON_RIGHT;
    case BTN_MIDDLE:
        return SDL_BUTTON_MIDDLE;
    default:
        return 0;
    }
}

/**
 * a new size from the server (the user resized or maximized it). the
 * buffer gets the new size when SDL makes its framebuffer again: SDL can
 * still draw into the old one until then.
 */
static void configure(SDL_Window *window, const heim_msg_t *msg)
{
    int32_t width = msg->configure.width;
    int32_t height = msg->configure.height;
    if (width > 0 && height > 0 && (width != window->w || height != window->h))
    {
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, width, height);
    }
    if (msg->configure.state & HEIM_STATE_MAXIMIZED)
    {
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_MAXIMIZED, 0, 0);
    }
    else if (window->flags & SDL_WINDOW_MAXIMIZED)
    {
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESTORED, 0, 0);
    }
}

static void key(SDL_Window *window, const heim_msg_t *msg)
{
    bool down = msg->key.value != HEIM_KEY_RELEASE;
    SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, msg->key.code, scancode_of(msg->key.code),
                        down);
    // the text a key types, for a window that takes text input
    if (down && msg->key.text[0] && SDL_TextInputActive(window) &&
        !(msg->key.mods & (HEIM_MOD_CTRL | HEIM_MOD_META)))
    {
        char text[sizeof(msg->key.text) + 1];
        SDL_memcpy(text, msg->key.text, sizeof(msg->key.text));
        text[sizeof(msg->key.text)] = '\0';
        SDL_SendKeyboardText(text);
    }
}

static void pointer(SDL_Window *window, const heim_msg_t *msg)
{
    float x = (float)msg->pointer.x;
    float y = (float)msg->pointer.y;
    switch (msg->pointer.kind)
    {
    case HEIM_POINTER_ENTER:
        SDL_SetMouseFocus(window);
        SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, false, x, y);
        break;
    case HEIM_POINTER_LEAVE:
        SDL_SetMouseFocus(NULL);
        break;
    case HEIM_POINTER_MOTION:
        SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, false, x, y);
        break;
    case HEIM_POINTER_PRESS:
    case HEIM_POINTER_RELEASE:
    {
        Uint8 button = button_of(msg->pointer.button);
        SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, false, x, y);
        if (button)
        {
            SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, button,
                                msg->pointer.kind == HEIM_POINTER_PRESS);
        }
        break;
    }
    case HEIM_POINTER_SCROLL:
        SDL_SendMouseWheel(0, window, SDL_DEFAULT_MOUSE_ID, 0.0f, (float)msg->pointer.value,
                           SDL_MOUSEWHEEL_NORMAL);
        break;
    default:
        break;
    }
}

/** act on one event of the connection */
static void handle_event(const heim_event_t *event)
{
    SDL_Window *window = sdl_window_of(event->window);
    if (!window)
    {
        return; // about no window of ours (the screen size, the settings)
    }
    switch (event->type)
    {
    case HEIM_EV_CONFIGURE:
        configure(window, &event->msg);
        break;
    case HEIM_EV_CLOSE:
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
        break;
    case HEIM_EV_FOCUS:
        SDL_SetKeyboardFocus(event->msg.focus.focused ? window : NULL);
        break;
    case HEIM_EV_KEY:
        key(window, &event->msg);
        break;
    case HEIM_EV_POINTER:
        pointer(window, &event->msg);
        break;
    default:
        break;
    }
}

static void GINNOS_PumpEvents(SDL_VideoDevice *_this)
{
    heim_event_t event;
    int got;
    while ((got = heim_next_event(_this->internal->conn, &event, 0)) > 0)
    {
        handle_event(&event);
    }
    if (got < 0 && !_this->internal->server_gone)
    {
        // the desktop stopped: the program cannot show anything more
        _this->internal->server_gone = true;
        SDL_SendQuit();
    }
}

static bool GINNOS_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window,
                                SDL_PropertiesID create_props)
{
    (void)create_props;
    SDL_WindowData *data = SDL_calloc(1, sizeof(*data));
    if (!data)
    {
        return false;
    }
    uint32_t flags = 0;
    if (window->flags & SDL_WINDOW_RESIZABLE)
    {
        flags |= HEIM_WINDOW_RESIZABLE;
    }
    if (window->flags & SDL_WINDOW_BORDERLESS)
    {
        flags |= HEIM_WINDOW_UNDECORATED;
    }
    data->heim = heim_window_new(_this->internal->conn, window->w, window->h,
                                 window->title ? window->title : "", flags);
    if (!data->heim)
    {
        SDL_free(data);
        return SDL_SetError("the window server made no window");
    }
    data->window = window;
    heim_window_set_data(data->heim, window);
    window->internal = data;
    return true;
}

static void GINNOS_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window->internal;
    if (data)
    {
        heim_window_free(data->heim);
        SDL_free(data);
        window->internal = NULL;
    }
}

static void GINNOS_SetWindowTitle(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    heim_window_set_title(window->internal->heim, window->title ? window->title : "");
}

/** the buffer gets the size in GINNOS_CreateWindowFramebuffer */
static void GINNOS_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->pending.w, window->pending.h);
}

/**
 * the framebuffer is the window's own buffer: its pixels are 0xAARRGGBB
 * words, and a window without HEIM_WINDOW_ALPHA shows them opaque, so
 * SDL can draw XRGB8888 into it.
 */
static bool GINNOS_CreateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window,
                                           SDL_PixelFormat *format, void **pixels, int *pitch)
{
    (void)_this;
    SDL_WindowData *data = window->internal;
    int w;
    int h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    gfx_buffer_t *buffer = heim_window_buffer(data->heim);
    if ((buffer->width != w || buffer->height != h) && !heim_window_resize(data->heim, w, h))
    {
        return SDL_OutOfMemory();
    }
    buffer = heim_window_buffer(data->heim);
    *format = SDL_PIXELFORMAT_XRGB8888;
    *pixels = buffer->pixels;
    *pitch = buffer->stride * 4;
    return true;
}

static bool GINNOS_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window,
                                           const SDL_Rect *rects, int numrects)
{
    (void)_this;
    if (numrects <= 0)
    {
        return true;
    }
    // one commit for the rectangle around them all
    SDL_Rect all = rects[0];
    for (int i = 1; i < numrects; i++)
    {
        SDL_GetRectUnion(&all, &rects[i], &all);
    }
    heim_window_commit(window->internal->heim, gfx_rect(all.x, all.y, all.w, all.h));
    return true;
}

static void GINNOS_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    // the buffer belongs to the window
    (void)_this;
    (void)window;
}

static bool GINNOS_SetClipboardText(SDL_VideoDevice *_this, const char *text)
{
    heim_clipboard_set(_this->internal->conn, text, SDL_strlen(text));
    return true;
}

/**
 * ask the server for the clipboard and wait for its answer; the events
 * that come before it are handled as usual.
 */
static char *GINNOS_GetClipboardText(SDL_VideoDevice *_this)
{
    heim_conn_t *conn = _this->internal->conn;
    heim_clipboard_request(conn);
    Uint64 until = SDL_GetTicks() + CLIPBOARD_WAIT_MS;
    heim_event_t event;
    for (Uint64 now = SDL_GetTicks(); now < until; now = SDL_GetTicks())
    {
        if (heim_next_event(conn, &event, (int)(until - now)) <= 0)
        {
            break;
        }
        if (event.type == HEIM_EV_CLIPBOARD)
        {
            return SDL_strdup(event.clipboard ? event.clipboard : "");
        }
        handle_event(&event);
    }
    return SDL_strdup("");
}

static bool GINNOS_HasClipboardText(SDL_VideoDevice *_this)
{
    char *text = GINNOS_GetClipboardText(_this);
    bool has = text && text[0];
    SDL_free(text);
    return has;
}

static bool GINNOS_VideoInit(SDL_VideoDevice *_this)
{
    heim_conn_t *conn = heim_connect();
    if (!conn)
    {
        return SDL_SetError("no window server (heimdalld) to connect to");
    }
    _this->internal->conn = conn;

    int32_t width;
    int32_t height;
    heim_screen_size(conn, &width, &height);
    SDL_DisplayMode mode;
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = width;
    mode.h = height;
    if (SDL_AddBasicVideoDisplay(&mode) == 0)
    {
        return false;
    }

    SDL_AddKeyboard(SDL_DEFAULT_KEYBOARD_ID, NULL);
    SDL_AddMouse(SDL_DEFAULT_MOUSE_ID, NULL);
    return true;
}

static void GINNOS_VideoQuit(SDL_VideoDevice *_this)
{
    if (_this->internal->conn)
    {
        heim_disconnect(_this->internal->conn);
        _this->internal->conn = NULL;
    }
}

static void GINNOS_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device->internal);
    SDL_free(device);
}

static SDL_VideoDevice *GINNOS_CreateDevice(void)
{
    SDL_VideoDevice *device = SDL_calloc(1, sizeof(*device));
    if (!device)
    {
        return NULL;
    }
    device->internal = SDL_calloc(1, sizeof(SDL_VideoData));
    if (!device->internal)
    {
        SDL_free(device);
        return NULL;
    }

    device->VideoInit = GINNOS_VideoInit;
    device->VideoQuit = GINNOS_VideoQuit;
    device->PumpEvents = GINNOS_PumpEvents;
    device->CreateSDLWindow = GINNOS_CreateWindow;
    device->DestroyWindow = GINNOS_DestroyWindow;
    device->SetWindowTitle = GINNOS_SetWindowTitle;
    device->SetWindowSize = GINNOS_SetWindowSize;
    device->CreateWindowFramebuffer = GINNOS_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = GINNOS_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = GINNOS_DestroyWindowFramebuffer;
    device->SetClipboardText = GINNOS_SetClipboardText;
    device->GetClipboardText = GINNOS_GetClipboardText;
    device->HasClipboardText = GINNOS_HasClipboardText;
    device->free = GINNOS_DeleteDevice;
    return device;
}

VideoBootStrap GINNOS_bootstrap = {
    "ginnos", "ginnOS windows (heimdall)", GINNOS_CreateDevice,
    NULL, // no message boxes of its own
    false
};

#endif // SDL_VIDEO_DRIVER_GINNOS

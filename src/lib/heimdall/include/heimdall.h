#pragma once

/**
 * @file heimdall.h
 * @brief libheimdall: windows on the ginnOS desktop (the heimdalld client).
 *
 * a program connects, makes windows, draws into their buffers with libgfx,
 * commits what changed, and gets events:
 *
 *     heim_conn_t *conn = heim_connect();
 *     heim_window_t *win = heim_window_new(conn, 400, 300, "Hello", 0);
 *     gfx_fill_rect(heim_window_buffer(win), gfx_rect(0, 0, 400, 300), color);
 *     heim_window_commit(win, gfx_rect(0, 0, 400, 300));
 *     heim_event_t ev;
 *     while (heim_next_event(conn, &ev, -1) > 0)
 *         if (ev.type == HEIM_EV_CLOSE)
 *             break;
 *
 * the protocol is in heimdall_proto.h.
 */

#include <gfx.h>
#include <heimdall_proto.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct heim_conn heim_conn_t;
typedef struct heim_window heim_window_t;

typedef struct
{
    uint16_t type;         // HEIM_EV_
    heim_window_t *window; // the window it is about, or NULL
    heim_msg_t msg;        // the fields (msg.key, msg.pointer, ...)
    char *clipboard;       // HEIM_EV_CLIPBOARD: the text, zero-terminated (or NULL);
                           // valid until the next heim_next_event
    size_t clipboard_length;
} heim_event_t;

/**
 * connect to the window server.
 * @return the connection, or NULL (no server: errno is set).
 */
heim_conn_t *heim_connect(void);

/**
 * close the connection. its windows go away (free them first, or not).
 */
void heim_disconnect(heim_conn_t *conn);

/**
 * the connection's socket, to wait for events with poll() together with
 * other files. POLLIN on it: call heim_next_event.
 */
int heim_fd(const heim_conn_t *conn);

void heim_screen_size(const heim_conn_t *conn, int32_t *width, int32_t *height);

/**
 * the next event.
 * @param timeout_ms how long to wait: -1 for as long as it takes, 0 not at all.
 * @return 1 with an event, 0 when the time ran out, -1 when the server is
 *         gone.
 */
int heim_next_event(heim_conn_t *conn, heim_event_t *event, int timeout_ms);

/**
 * make a window with a buffer of that size (its content, without the
 * title bar). it shows after the first heim_window_commit.
 * @param flags HEIM_WINDOW_RESIZABLE, HEIM_WINDOW_ALPHA, HEIM_WINDOW_UNDECORATED.
 */
heim_window_t *heim_window_new(heim_conn_t *conn, int32_t width, int32_t height,
                               const char *title, uint32_t flags);

void heim_window_free(heim_window_t *win);

/**
 * the buffer to draw into. after heim_window_resize it is another one.
 */
gfx_buffer_t *heim_window_buffer(heim_window_t *win);

/**
 * a new buffer of another size (its pixels are transparent). the old
 * content stays on the screen until the next commit. for HEIM_EV_CONFIGURE,
 * or when the program wants another size.
 * @return false when out of memory (the old buffer stays).
 */
bool heim_window_resize(heim_window_t *win, int32_t width, int32_t height);

/**
 * show a changed part of the buffer.
 */
void heim_window_commit(heim_window_t *win, gfx_rect_t damage);

/**
 * true from a commit until its HEIM_EV_FRAME: drawing then can be seen
 * half done. an animation draws its next frame when this is false.
 */
bool heim_window_busy(const heim_window_t *win);

void heim_window_set_title(heim_window_t *win, const char *title);

/**
 * the pointer shape over the window's content (HEIM_CURSOR_).
 */
void heim_window_set_cursor(heim_window_t *win, uint32_t shape);

/**
 * a pointer of the program's own for the window (NULL at first).
 */
void heim_window_set_data(heim_window_t *win, void *data);
void *heim_window_data(const heim_window_t *win);

/**
 * put text on the clipboard (length 0 empties it).
 */
void heim_clipboard_set(heim_conn_t *conn, const char *text, size_t length);

/**
 * ask for the clipboard's text: it comes as a HEIM_EV_CLIPBOARD event.
 */
void heim_clipboard_request(heim_conn_t *conn);

/**
 * tell all clients that the settings file name changed (after conf_save):
 * each gets a HEIM_EV_SETTINGS event with the name, also this one.
 * @param name the libconf name, "theme".
 */
void heim_settings_changed(heim_conn_t *conn, const char *name);

<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# heimdall

libheimdall: windows on the desktop, and the window server protocol.

## &lt;heimdall.h&gt;

Source: [`src/lib/heimdall/include/heimdall.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/heimdall/include/heimdall.h)

libheimdall: windows on the ginnOS desktop (the heimdalld client).

a program connects, makes windows, draws into their buffers with libgfx,
commits what changed, and gets events:

```c
heim_conn_t *conn = heim_connect();
heim_window_t *win = heim_window_new(conn, 400, 300, "Hello", 0);
gfx_fill_rect(heim_window_buffer(win), gfx_rect(0, 0, 400, 300), color);
heim_window_commit(win, gfx_rect(0, 0, 400, 300));
heim_event_t ev;
while (heim_next_event(conn, &ev, -1) > 0)
    if (ev.type == HEIM_EV_CLOSE)
        break;
```

the protocol is in heimdall\_proto.h.

### heim\_connect

```c
heim_conn_t *heim_connect(void);
```

connect to the window server.

Returns: the connection, or NULL (no server: errno is set).

### heim\_disconnect

```c
void heim_disconnect(heim_conn_t *conn);
```

close the connection. its windows go away (free them first, or not).

### heim\_fd

```c
int heim_fd(const heim_conn_t *conn);
```

the connection's socket, to wait for events with poll() together with
other files. POLLIN on it: call heim\_next\_event.

### heim\_pending

```c
bool heim_pending(const heim_conn_t *conn);
```

true if a whole message is already read from the socket: the next
heim\_next\_event returns at once. a loop that polls heim\_fd with other
fds checks this first.

### heim\_next\_event

```c
int heim_next_event(heim_conn_t *conn, heim_event_t *event, int timeout_ms);
```

the next event.

- `timeout_ms`: how long to wait: -1 for as long as it takes, 0 not at all.

Returns: 1 with an event, 0 when the time ran out, -1 when the server is gone.

### heim\_window\_new

```c
heim_window_t *heim_window_new(heim_conn_t *conn, int32_t width, int32_t height,
                               const char *title, uint32_t flags);
```

make a window with a buffer of that size (its content, without the
title bar). it shows after the first heim\_window\_commit.

- `flags`: HEIM\_WINDOW\_RESIZABLE, HEIM\_WINDOW\_ALPHA, HEIM\_WINDOW\_UNDECORATED.

### heim\_popup\_new

```c
heim_window_t *heim_popup_new(heim_window_t *parent, int32_t width, int32_t height,
                              uint32_t flags);
```

make a popup of parent (a menu, a popover; with HEIM\_WINDOW\_TOOLTIP a
tooltip). place it with heim\_window\_place before its first commit.

### heim\_window\_place

```c
void heim_window_place(heim_window_t *popup, gfx_rect_t r, uint32_t side, int32_t margin);
```

where a popup goes: next to the rect r of its parent's content, on a
side (HEIM\_SIDE\_). margin: the shadow around its card in its buffer.

### heim\_window\_buffer

```c
gfx_buffer_t *heim_window_buffer(heim_window_t *win);
```

the buffer to draw into. after heim\_window\_resize it is another one.

### heim\_window\_resize

```c
bool heim_window_resize(heim_window_t *win, int32_t width, int32_t height);
```

a new buffer of another size (its pixels are transparent). the old
content stays on the screen until the next commit. for HEIM\_EV\_CONFIGURE,
or when the program wants another size.

Returns: false when out of memory (the old buffer stays).

### heim\_window\_commit

```c
void heim_window_commit(heim_window_t *win, gfx_rect_t damage);
```

show a changed part of the buffer.

### heim\_window\_busy

```c
bool heim_window_busy(const heim_window_t *win);
```

true from a commit until its HEIM\_EV\_FRAME: drawing then can be seen
half done. an animation draws its next frame when this is false.

### heim\_window\_set\_cursor

```c
void heim_window_set_cursor(heim_window_t *win, uint32_t shape);
```

the pointer shape over the window's content (HEIM\_CURSOR\_).

### heim\_window\_set\_data, heim\_window\_data

```c
void heim_window_set_data(heim_window_t *win, void *data);
void *heim_window_data(const heim_window_t *win);
```

a pointer of the program's own for the window (NULL at first).

### heim\_clipboard\_set

```c
void heim_clipboard_set(heim_conn_t *conn, const char *text, size_t length);
```

put text on the clipboard (length 0 empties it).

### heim\_clipboard\_request

```c
void heim_clipboard_request(heim_conn_t *conn);
```

ask for the clipboard's text: it comes as a HEIM\_EV\_CLIPBOARD event.

### heim\_settings\_changed

```c
void heim_settings_changed(heim_conn_t *conn, const char *name);
```

tell all clients that the settings file name changed (after conf\_save):
each gets a HEIM\_EV\_SETTINGS event with the name, also this one.

- `name`: the libconf name, "theme".

### heim\_window\_set\_app

```c
void heim_window_set_app(heim_window_t *win, const char *app);
```

name the program of a window, so the shell can show its icon (the id of
its .app file: "idunn-gallery").

### heim\_shell\_start

```c
void heim_shell_start(heim_conn_t *conn);
```

be the desktop shell: from now on HEIM\_EV\_WINDOW, HEIM\_EV\_WINDOW\_APP
and HEIM\_EV\_WINDOW\_GONE tell about every normal window (the event's
window is NULL; msg.info.id names the window). only the first client
that asks is the shell.

### heim\_shell\_act

```c
void heim_shell_act(heim_conn_t *conn, uint32_t id, uint32_t action);
```

the shell does something to a window.

- `id`: msg.info.id of the window.
- `action`: HEIM\_ACT\_ACTIVATE, HEIM\_ACT\_MINIMIZE or HEIM\_ACT\_CLOSE.

### heim\_shell\_quit

```c
void heim_shell_quit(heim_conn_t *conn);
```

the shell ends the session: heimdalld stops and the text console comes
back.

## &lt;heimdall_proto.h&gt;

Source: [`src/lib/heimdall/include/heimdall_proto.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/heimdall/include/heimdall_proto.h)

the messages between heimdalld (the window server) and its clients.

a client connects to the Unix socket HEIM\_SOCKET. every message, in both
directions, is one heim\_msg\_t of 64 bytes. a message that carries an open
file (a shared memory buffer) has it in an SCM\_RIGHTS control message
sent with it.

the start: the client sends HEIM\_REQ\_HELLO, the server answers with
HEIM\_EV\_WELCOME (the screen size).

a window: the client picks a window number (not 0, its own), sends
HEIM\_REQ\_CREATE, then HEIM\_REQ\_ATTACH with a shared memory buffer of
premultiplied ARGB pixels, draws into it, and sends HEIM\_REQ\_COMMIT with
the part it changed. the window shows after its first commit. after a
commit, the server sends HEIM\_EV\_FRAME when the change is on the
screen; a client that draws again before that can be seen half drawn.

the server draws the title bar and the buttons (server-side
decorations); the size of a window is the size of its content. a window
is placed by the server. HEIM\_EV\_CONFIGURE asks the client for another
size (maximize, a resize with the mouse): the client attaches a buffer
of that size and commits it.

a popup (a menu, a select's choices, a popover, a tooltip) is a window
with HEIM\_WINDOW\_POPUP and a parent window. it has no decorations and
its pixels can be see-through; the client draws its shadow in a margin
around it. HEIM\_REQ\_PLACE gives a rect in the parent's content to show
it next to and a side; the server puts it there, on the other side
when there is no room, and inside the screen. a popup is above its
parent and gets the keyboard (a tooltip, HEIM\_WINDOW\_TOOLTIP, gets no
input at all). a press outside the open popups closes them: each gets
HEIM\_EV\_CLOSE, and the press goes nowhere else.

a panel (HEIM\_WINDOW\_PANEL) is a bar of the desktop shell: the server
puts it at the bottom of the screen, the full width (it sends
HEIM\_EV\_CONFIGURE for the width), above the windows. its height is not
part of the work area: maximized and new windows stay above it. a click
on it does not take the keyboard from the window that has it.

the shell: one client can ask to be the shell (HEIM\_REQ\_SHELL, the
first that asks). it then gets HEIM\_EV\_WINDOW for each normal window
when the window shows or changes (title, state), HEIM\_EV\_WINDOW\_APP
with the program of the window, and HEIM\_EV\_WINDOW\_GONE; the window
field is 0 and info.id is the server's number for the window. the shell
can activate, minimize and close any window (HEIM\_REQ\_ACT) and end the
session (HEIM\_REQ\_QUIT).

This header has no doc comments yet.

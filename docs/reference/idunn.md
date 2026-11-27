<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# idunn

libidunn: the widget toolkit and the theme.

## &lt;idunn.h&gt;

Source: [`src/lib/idunn/include/idunn.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/idunn/include/idunn.h)

idunn: the widget toolkit of ginnOS.

an app makes a tree of widgets (rows and columns of labels, buttons and
so on), puts it in a window and runs the app loop. widgets keep their
state between frames; the toolkit lays them out, draws only what
changed, in step with the screen, and sends the pointer and the keys to
them. the look comes from the theme (idunn\_theme.h).

```c
idunn_app_t *app = idunn_app_new("Files");
idunn_window_t *win = idunn_window_new(app, "Files", 720, 480);
idunn_widget_t *col = idunn_column(4, 6);
idunn_add(col, idunn_label("Rename file", IDUNN_TEXT_HEADING));
idunn_add(col, idunn_button("Rename", IDUNN_PRIMARY, on_rename, NULL));
idunn_window_set_root(win, col);
return idunn_app_run(app);
```

sizes are in design pixels (the theme's scale makes them larger);
gaps and paddings are in space units of 4 design pixels.

### idunn\_app\_new

```c
idunn_app_t *idunn_app_new(const char *name);
```

connect to the window server and read the theme.

- `name`: the program's id, the name of its .app file ("idunn-gallery"): the shell shows its icon with its windows.

Returns: the app, or NULL (no server, or no fonts).

### idunn\_app\_conn

```c
heim_conn_t *idunn_app_conn(const idunn_app_t *app);
```

the connection to the window server, for calls idunn does not wrap
(the shell's heim\_shell\_start, for example).

### on\_event

```c
void idunn_app_on_event(idunn_app_t *app,
                        void (*on_event)(idunn_app_t *app, const heim_msg_t *msg, void *data),
                        void *data);
```

get the events that are about no window of the app (the shell's
HEIM\_EV\_WINDOW, HEIM\_EV\_WINDOW\_APP, HEIM\_EV\_WINDOW\_GONE), and
HEIM\_EV\_SETTINGS (after idunn applied a new theme).

### every

```c
void idunn_app_every(idunn_app_t *app, uint32_t period, void (*every)(void *data), void *data);
```

call every(data) each period ms while the app runs (a clock). one per
app; a new call replaces it, period 0 stops it.

### ready

```c
void idunn_app_watch(idunn_app_t *app, int fd, void (*ready)(int fd, void *data), void *data);
```

call ready(fd, data) when fd can be read (or has hung up) while the app
runs: a pty, a pipe. at most IDUNN\_WATCH\_MAX at a time; ready NULL
stops watching fd.

### got

```c
void idunn_app_paste(idunn_app_t *app, void (*got)(const char *text, size_t length, void *data),
                     void *data);
```

ask for the clipboard's text for the program itself (not a widget):
got gets it when it comes ("" when the clipboard is empty).

### idunn\_app\_run, idunn\_app\_quit

```c
int idunn_app_run(idunn_app_t *app);
void idunn_app_quit(idunn_app_t *app);
```

run until the last window is closed or idunn\_app\_quit.

Returns: the exit status for main.

### idunn\_app\_set\_theme

```c
void idunn_app_set_theme(idunn_app_t *app, const idunn_theme_t *theme);
```

use another theme in all windows of the app (they are drawn again).

### idunn\_app\_save\_theme, idunn\_app\_theme

```c
bool idunn_app_save_theme(idunn_app_t *app, const char *key, const char *value);
const idunn_theme_t *idunn_app_theme(const idunn_app_t *app);
```

change a theme setting (mode, accent, radius, font\_size, scale, motion)
in the user's theme file. every app and the window decorations change.

Returns: false if the file cannot be written; this app changes then.

### idunn\_window\_new

```c
idunn_window_t *idunn_window_new(idunn_app_t *app, const char *title, int32_t width,
                                 int32_t height);
```

a window with a title bar; it shows when the app runs.

- `width`: the size of its content, in design pixels.

### idunn\_screen\_window\_new

```c
idunn_window_t *idunn_screen_window_new(idunn_app_t *app, const gfx_buffer_t *backdrop);
```

a window as large as the screen with no title bar (a login screen).

- `backdrop`: the picture under the widgets, as large as the screen (or NULL: the surface colour). it must stay until the app ends.

### idunn\_panel\_new

```c
idunn_window_t *idunn_panel_new(idunn_app_t *app, int32_t height);
```

a panel of the desktop shell: no title bar, at the bottom of the screen,
as wide as the screen, above the windows (HEIM\_WINDOW\_PANEL).

- `height`: in design pixels.

### idunn\_window\_set\_root

```c
void idunn_window_set_root(idunn_window_t *win, idunn_widget_t *root);
```

the widget that fills the window (a row or a column, usually). the
window owns the tree from now on.

### idunn\_free\_later

```c
void idunn_free_later(idunn_widget_t *w);
```

free w (and its children) after the event that is running: for a
widget that may be in its own callback (a button that rebuilds the
view it is in).

### idunn\_widget\_key\_t, idunn\_on\_key

```c
typedef bool (*idunn_widget_key_t)(idunn_widget_t *w, uint16_t code, uint32_t mods, void *data);
void idunn_on_key(idunn_widget_t *w, idunn_widget_key_t on_key, void *data);
```

keys for a widget (when it or a child has the keyboard) before the
widget gets them. return true to use the key (the widget does not get
it), false to let it go on as before.

### idunn\_key\_hook\_t, idunn\_window\_on\_key

```c
typedef bool (*idunn_key_hook_t)(idunn_window_t *win, uint16_t code, uint32_t mods,
                                 const char *text, void *data);
void idunn_window_on_key(idunn_window_t *win, idunn_key_hook_t on_key, void *data);
```

keys that no widget used (the focused one and its parents): the
window's own shortcuts. on\_key returns true when it used the key.
code is a Linux key code, mods IDUNN\_MOD\_, text what it types or "".

### idunn\_window\_close

```c
void idunn_window_close(idunn_window_t *win);
```

close and free the window (and its widgets).

### on\_close

```c
void idunn_window_on_close(idunn_window_t *win, bool (*on_close)(idunn_window_t *win, void *data),
                           void *data);
```

the close button (or the shell's Close): on\_close returns true to let
the window close, false to keep it (to ask about unsaved changes first,
and close it later with idunn\_window\_close).

### idunn\_window\_set\_title

```c
void idunn_window_set_title(idunn_window_t *win, const char *title);
```

a new title in the window's title bar (and the shell's window list).

### idunn\_row, idunn\_column

```c
idunn_widget_t *idunn_row(int gap, int padding);
idunn_widget_t *idunn_column(int gap, int padding);
```

a box that puts its children in a row (left to right) or a column (top
to bottom), like a CSS flexbox.

- `gap`: the space between children, in units (4 design pixels).
- `padding`: the space inside the box, in units.

### idunn\_box\_set\_align

```c
void idunn_box_set_align(idunn_widget_t *box, idunn_align_t align, idunn_align_t justify);
```

how a box places its children: align across it (default: stretch) and
justify along it (default: start).

### idunn\_box\_set\_padding

```c
void idunn_box_set_padding(idunn_widget_t *box, int vertical, int horizontal);
```

other paddings for the sides of a box, in units: across and along.

### idunn\_box\_set\_background

```c
void idunn_box_set_background(idunn_widget_t *box, idunn_color_t color);
```

a background for a box, a colour token (by default it has none).

### idunn\_box\_set\_keep\_end

```c
void idunn_box_set_keep_end(idunn_widget_t *box, bool keep_end);
```

a row that is too short for its children shows the last ones: the
first go out of view on the left, whole (a path bar keeps the folder
that is open in view).

### idunn\_card

```c
idunn_widget_t *idunn_card(int gap);
```

a card: a column on the card colour with a border, the large radius and
a small shadow, 24 pixels inside.

- `gap`: the space between its children, in units.

### idunn\_card\_set\_glass

```c
void idunn_card_set_glass(idunn_widget_t *card, bool glass);
```

frosted glass: the card is see-through, lighter than what is under it,
with a thin light edge and a larger shadow (over a blurred picture, as
on a login screen).

### idunn\_card\_header

```c
void idunn_card_header(idunn_widget_t *card, const char *title, const char *description);
```

a title and a muted description (or NULL) at the top of a card.

### idunn\_scroll\_view, idunn\_scroll\_view\_offset, idunn\_scroll\_view\_scroll\_to

```c
idunn_widget_t *idunn_scroll_view(idunn_widget_t *child);
int32_t idunn_scroll_view_offset(const idunn_widget_t *view);
void idunn_scroll_view_scroll_to(idunn_widget_t *view, int32_t offset);
```

a scroll view: the child can be taller than the view. the view takes
the height it is given (let it grow, or give it a size), as a CSS box
with min-height 0. the wheel, the keys its children do not use and a
thin scroll bar scroll it; Tab scrolls the focus into view.

### idunn\_spacer

```c
idunn_widget_t *idunn_spacer(void);
```

empty space that grows (put it between children to push them apart).

### idunn\_add

```c
idunn_widget_t *idunn_add(idunn_widget_t *parent, idunn_widget_t *child);
```

add child at the end of parent.

Returns: child.

### idunn\_widget\_free

```c
void idunn_widget_free(idunn_widget_t *w);
```

take a widget out of its parent and free it and its children.

### idunn\_grow

```c
idunn_widget_t *idunn_grow(idunn_widget_t *w, int weight);
```

let a widget take a share of the free space along its box.

Returns: w.

### idunn\_set\_size

```c
void idunn_set_size(idunn_widget_t *w, int32_t width, int32_t height);
```

a size a widget must have, in design pixels (0: its own size).

### idunn\_set\_hidden

```c
void idunn_set_hidden(idunn_widget_t *w, bool hidden);
```

a hidden widget is not drawn, takes no room in its box and gets no input.

### idunn\_set\_disabled

```c
void idunn_set_disabled(idunn_widget_t *w, bool disabled);
```

a disabled widget is drawn at half opacity and gets no input.

### idunn\_on\_menu

```c
void idunn_on_menu(idunn_widget_t *w, idunn_callback_t on_menu, void *data);
```

on\_menu on a right click on the widget or in it, when no widget in it
used the click (a list view uses it on its items: idunn\_list\_view\_on\_menu).
open a menu with idunn\_menu\_popup\_at\_pointer.

### idunn\_on\_middle\_click

```c
void idunn_on_middle_click(idunn_widget_t *w, idunn_callback_t on_middle, void *data);
```

on\_middle on a press of the middle button on the widget or in it (the
nearest widget that has one): a task button closes its window.

### idunn\_set\_error

```c
void idunn_set_error(idunn_widget_t *w, bool error);
```

a control with a wrong value: its border and ring are red (a field
sets this on its control with idunn\_field\_set\_error).

### idunn\_label, idunn\_label\_set\_text, idunn\_label\_set\_color, idunn\_label\_set\_wrap

```c
idunn_widget_t *idunn_label(const char *text, idunn_text_t style);
void idunn_label_set_text(idunn_widget_t *label, const char *text);
void idunn_label_set_color(idunn_widget_t *label, idunn_color_t color);
void idunn_label_set_wrap(idunn_widget_t *label, bool wrap);
```

text in a style; it ends with "..." when it is too wide, or wraps.

### idunn\_button

```c
idunn_widget_t *idunn_button(const char *text, idunn_variant_t variant, idunn_callback_t on_click,
                             void *data);
```

a button; on\_click gets the button and data.

- `text`: the label, or NULL for an icon button.

### idunn\_icon\_button

```c
idunn_widget_t *idunn_icon_button(const char *icon, idunn_variant_t variant,
                                  idunn_callback_t on_click, void *data);
```

a square button with only an icon (a Lucide name).

### idunn\_button\_set\_icon

```c
void idunn_button_set_icon(idunn_widget_t *button, const char *icon);
```

an icon (a Lucide name) before the text.

### idunn\_button\_set\_picture

```c
void idunn_button_set_picture(idunn_widget_t *button, const char *picture);
```

a picture in its own colours (a Tela name, an app's icon) before the text

### idunn\_button\_set\_active

```c
void idunn_button_set_active(idunn_widget_t *button, bool active);
```

the button is on (a task's window is active, a toggle is on): a
filled look with a short accent line under it.

### idunn\_button\_set\_dim

```c
void idunn_button_set_dim(idunn_widget_t *button, bool dim);
```

the content muted (a task whose window is minimized)

### idunn\_button\_set\_pill

```c
void idunn_button_set_pill(idunn_widget_t *button, bool pill);
```

round ends: the radius is half the height

### idunn\_button\_set\_glass

```c
void idunn_button_set_glass(idunn_widget_t *button, bool glass);
```

see-through, lighter than what is under it (over a picture)

### idunn\_button\_set\_tile, idunn\_button\_set\_size, idunn\_button\_set\_text, idunn\_button\_set\_variant

```c
void idunn_button_set_tile(idunn_widget_t *button, bool tile);
void idunn_button_set_size(idunn_widget_t *button, idunn_size_t size);
void idunn_button_set_text(idunn_widget_t *button, const char *text);
void idunn_button_set_variant(idunn_widget_t *button, idunn_variant_t variant);
```

a tile: the icon larger, over the text in small type (Restart, Power off)

### idunn\_button\_set\_align

```c
void idunn_button_set_align(idunn_widget_t *button, idunn_align_t align);
```

where the icon and text go: IDUNN\_CENTER (the default) or IDUNN\_START
(side bar items, menu-like lists).

### idunn\_separator

```c
idunn_widget_t *idunn_separator(void);
```

a 1 pixel line: across a column, or down a row.

### idunn\_text\_field

```c
idunn_widget_t *idunn_text_field(const char *placeholder);
```

a one-line text field, 32 pixels high.

- `placeholder`: the muted text it shows when it is empty, or NULL.

### idunn\_text\_field\_text, idunn\_text\_field\_set\_text

```c
const char *idunn_text_field_text(const idunn_widget_t *field);
void idunn_text_field_set_text(idunn_widget_t *field, const char *text);
```

the text (UTF-8); valid until the text changes

### idunn\_text\_field\_set\_plain

```c
void idunn_text_field_set_plain(idunn_widget_t *field, bool plain);
```

a plain field: a thin box around the text, with little padding and no
ring (a name that is changed in place, as in a file list).

### idunn\_text\_field\_set\_pill

```c
void idunn_text_field_set_pill(idunn_widget_t *field, bool pill);
```

a pill: round ends, see-through (over a picture, as on a login screen),
no border, the text in the middle while it fits.

### idunn\_text\_field\_set\_secret

```c
void idunn_text_field_set_secret(idunn_widget_t *field, bool secret);
```

a password field: a bullet for each character, and no Copy or Cut

### idunn\_text\_field\_text\_width, idunn\_text\_field\_select

```c
int32_t idunn_text_field_text_width(idunn_widget_t *field);
void idunn_text_field_select(idunn_widget_t *field, size_t start, size_t end);
```

the width of the field's text in pixels (0 before it is in a window)

### idunn\_text\_field\_set\_icon

```c
void idunn_text_field_set_icon(idunn_widget_t *field, const char *icon);
```

an icon (a Lucide name) at the start, as in a search field

### idunn\_text\_field\_on\_change, idunn\_text\_field\_on\_submit

```c
void idunn_text_field_on_change(idunn_widget_t *field, idunn_callback_t on_change, void *data);
void idunn_text_field_on_submit(idunn_widget_t *field, idunn_callback_t on_submit, void *data);
```

on\_change after every change of the text; on\_submit on Enter

### idunn\_text\_field\_on\_blur

```c
void idunn_text_field_on_blur(idunn_widget_t *field, idunn_callback_t on_blur, void *data);
```

on\_blur when the field loses the keyboard

### idunn\_text\_area, idunn\_text\_area\_text, idunn\_text\_area\_set\_text, idunn\_text\_area\_on\_change

```c
idunn_widget_t *idunn_text_area(const char *placeholder, int rows);
const char *idunn_text_area_text(const idunn_widget_t *area);
void idunn_text_area_set_text(idunn_widget_t *area, const char *text);
void idunn_text_area_on_change(idunn_widget_t *area, idunn_callback_t on_change, void *data);
```

a text area: lines that wrap, Enter for a new line; it scrolls.

- `rows`: the lines it shows (its height), 0 for 4.

### idunn\_check\_box, idunn\_check\_box\_state, idunn\_check\_box\_set\_state

```c
idunn_widget_t *idunn_check_box(const char *label, bool checked, idunn_callback_t on_change,
                                void *data);
idunn_check_t idunn_check_box_state(const idunn_widget_t *box);
void idunn_check_box_set_state(idunn_widget_t *box, idunn_check_t state);
```

a check box with a label to its right (a click on the label clicks it).
on\_change comes after a click or Space.

### idunn\_radio\_group, idunn\_radio\_group\_chosen, idunn\_radio\_group\_choose, idunn\_radio\_group\_set\_row

```c
idunn_widget_t *idunn_radio_group(const char *const *labels, int count, int chosen,
                                  idunn_callback_t on_change, void *data);
int idunn_radio_group_chosen(const idunn_widget_t *group);
void idunn_radio_group_choose(idunn_widget_t *group, int chosen);
void idunn_radio_group_set_row(idunn_widget_t *group, bool row);
```

a group of choices with round buttons, one chosen; one Tab stop, the
arrows move the choice. in a column (or a row: idunn\_radio\_group\_set\_row).

- `chosen`: the index of the chosen one, or -1 for none.

### idunn\_switch, idunn\_switch\_on, idunn\_switch\_set\_on

```c
idunn_widget_t *idunn_switch(const char *label, bool on, idunn_callback_t on_change, void *data);
bool idunn_switch_on(const idunn_widget_t *sw);
void idunn_switch_set_on(idunn_widget_t *sw, bool on);
```

a switch with a label to its right; it slides when it changes.

### idunn\_slider, idunn\_slider\_value, idunn\_slider\_set\_value

```c
idunn_widget_t *idunn_slider(int32_t min, int32_t max, int32_t step, int32_t value,
                             idunn_callback_t on_change, void *data);
int32_t idunn_slider_value(const idunn_widget_t *slider);
void idunn_slider_set_value(idunn_widget_t *slider, int32_t value);
```

a slider from min to max in steps (the arrows move it by one step).

### idunn\_badge

```c
idunn_widget_t *idunn_badge(const char *text, idunn_badge_t variant);
```

a badge: a short word in a small pill

### idunn\_icon\_view

```c
idunn_widget_t *idunn_icon_view(const char *name, int32_t size, idunn_color_t color);
```

a Lucide icon on its own, size points, in a colour token

### idunn\_empty\_state

```c
idunn_widget_t *idunn_empty_state(const char *icon, const char *title, const char *text);
```

an empty state: an icon on a muted square, a title and a muted line (or
NULL), in the middle of its space. add buttons to it for actions.

### idunn\_progress, idunn\_progress\_set

```c
idunn_widget_t *idunn_progress(void);
void idunn_progress_set(idunn_widget_t *progress, int32_t value);
```

a progress bar across its box; value 0 to IDUNN\_PROGRESS\_MAX

### idunn\_tabs

```c
idunn_widget_t *idunn_tabs(idunn_callback_t on_change, void *data);
```

tabs: a strip of names and the page of the active one below it. the
strip is one Tab stop; the arrows change the tab.

### idunn\_tabs\_add, idunn\_tabs\_active, idunn\_tabs\_set\_active

```c
idunn_widget_t *idunn_tabs_add(idunn_widget_t *tabs, const char *name, idunn_widget_t *page);
int idunn_tabs_active(const idunn_widget_t *tabs);
void idunn_tabs_set_active(idunn_widget_t *tabs, int active);
```

add a tab with its page (the tabs own it); the first one is active

### idunn\_list\_view

```c
idunn_widget_t *idunn_list_view(void);
```

a list view: rows with a Lucide icon (or NULL), text and a muted second
text on the right (or NULL). put it in a scroll view when it is long.

### idunn\_grid\_view

```c
idunn_widget_t *idunn_grid_view(void);
```

a grid view: the same, as tiles of a 48 point Tela icon (a file or
folder icon name) with the name below; it wraps to the width.

### idunn\_list\_view\_add, idunn\_list\_view\_clear

```c
int idunn_list_view_add(idunn_widget_t *list, const char *icon, const char *text,
                        const char *detail);
void idunn_list_view_clear(idunn_widget_t *list);
```

Returns: the index of the new item, or -1

### idunn\_list\_view\_set\_item

```c
void idunn_list_view_set_item(idunn_widget_t *list, int item, const char *icon, const char *text,
                              const char *detail);
```

change an item; only its row is drawn again, and only when something
differs (a list that is filled again each second, as in a monitor).

### idunn\_list\_view\_set\_count, idunn\_list\_view\_count, idunn\_list\_view\_text

```c
void idunn_list_view_set_count(idunn_widget_t *list, int count);
int idunn_list_view_count(const idunn_widget_t *list);
const char *idunn_list_view_text(const idunn_widget_t *list, int item);
```

keep only the first count items (a smaller count than now)

### idunn\_list\_view\_set\_multiple

```c
void idunn_list_view_set_multiple(idunn_widget_t *list, bool multiple);
```

many items can be selected (Ctrl, Shift, Ctrl+A)

### idunn\_list\_view\_set\_pictures

```c
void idunn_list_view_set_pictures(idunn_widget_t *list, bool pictures);
```

rows with a Tela icon (a file or folder icon name) in colour, not a Lucide one

### idunn\_list\_view\_set\_framed, idunn\_list\_view\_selected, idunn\_list\_view\_select

```c
void idunn_list_view_set_framed(idunn_widget_t *list, bool framed);
bool idunn_list_view_selected(const idunn_widget_t *list, int item);
void idunn_list_view_select(idunn_widget_t *list, int item, bool selected);
```

a border, the large radius and the card colour around the items

### idunn\_list\_view\_current

```c
int idunn_list_view_current(const idunn_widget_t *list);
```

the item with the keyboard, or -1

### idunn\_list\_view\_on\_select, idunn\_list\_view\_on\_activate

```c
void idunn_list_view_on_select(idunn_widget_t *list, idunn_callback_t on_select, void *data);
void idunn_list_view_on_activate(idunn_widget_t *list, idunn_callback_t on_activate, void *data);
```

on\_select after the selection changes; on\_activate on a double click or Enter

### idunn\_edit\_done\_t

```c
typedef void (*idunn_edit_done_t)(idunn_widget_t *list, int item, const char *text, void *data);
```

what an edit in place gives: the new text, or NULL when it was taken
back (Escape). it comes after the event, so the list can be made again.

### idunn\_list\_view\_edit

```c
bool idunn_list_view_edit(idunn_widget_t *list, int item, size_t start, size_t end,
                          idunn_edit_done_t done, void *data);
```

edit an item's text in place (a rename): a text field over it with the
bytes start to end selected ((size\_t)-1: to the end). Enter, or the
keyboard going elsewhere, keeps the text; Escape takes it back.

Returns: false when the item is not there or the list is not shown.

### idunn\_list\_view\_set\_columns

```c
void idunn_list_view_set_columns(idunn_widget_t *list, const int32_t *widths, int count);
```

columns at the right of the rows: an item's detail is split at tabs,
and each part is drawn at the right of its column (numbers line up).

- `widths`: the columns' widths in design pixels, left to right.

### idunn\_list\_heads

```c
idunn_widget_t *idunn_list_heads(idunn_widget_t *list, const char *const *names, int count,
                                 idunn_callback_t on_click, void *data);
```

heads over the columns of a list view, which line up with it. a click
on a head calls on\_click; idunn\_list\_heads\_clicked tells which.

- `names`: the name column's head, then one for each column of idunn\_list\_view\_set\_columns.

### idunn\_list\_heads\_clicked

```c
int idunn_list_heads_clicked(const idunn_widget_t *heads);
```

the head that was clicked: 0 for the names, 1 and on for the columns

### idunn\_list\_heads\_set\_order

```c
void idunn_list_heads_set_order(idunn_widget_t *heads, int head, bool descending);
```

show the order on a head (an arrow), or on none with head -1

### idunn\_list\_view\_set\_columns\_left

```c
void idunn_list_view_set_columns_left(idunn_widget_t *list, uint32_t mask);
```

draw some columns at their left, for text (a date, a type); the others
stay at their right, for numbers. bit c is column c.

### idunn\_list\_view\_go\_to

```c
void idunn_list_view_go_to(idunn_widget_t *list, int item);
```

make an item the current one and select only it, as the arrow keys do

### idunn\_list\_view\_columns

```c
int idunn_list_view_columns(const idunn_widget_t *list);
```

the items in a line (1 for a list, more for a grid)

### idunn\_list\_view\_editing

```c
bool idunn_list_view_editing(const idunn_widget_t *list);
```

true while an item is edited in place

### idunn\_list\_view\_set\_single\_click

```c
void idunn_list_view_set_single_click(idunn_widget_t *list, bool single);
```

one click activates an item (a launcher); the arrow keys only move

### idunn\_list\_view\_on\_menu

```c
void idunn_list_view_on_menu(idunn_widget_t *list, idunn_callback_t on_menu, void *data);
```

on\_menu on a right click: on an item that is not selected, it selects
only that item first; on no item, nothing stays selected. open a menu
with idunn\_menu\_popup\_at\_pointer.

### idunn\_list\_view\_item\_at

```c
int idunn_list_view_item_at(const idunn_widget_t *list, int32_t x, int32_t y);
```

the item at a point of the window, or -1

### idunn\_menu

```c
typedef struct idunn_menu idunn_menu_t;
```

a menu: items the app adds, shown in a popup by idunn\_menu\_popup

### idunn\_menu\_free

```c
void idunn_menu_free(idunn_menu_t *menu);
```

free a menu (not while it is open) and its sub-menus

### idunn\_menu\_add

```c
int idunn_menu_add(idunn_menu_t *menu, const char *icon, const char *text, const char *shortcut,
                   idunn_callback_t on_choose, void *data);
```

an item: a Lucide icon (or NULL), text and a shortcut to show (or NULL).
on\_choose gets the widget the menu was opened for.

Returns: the index of the item.

### idunn\_menu\_add\_check, idunn\_menu\_add\_separator

```c
int idunn_menu_add_check(idunn_menu_t *menu, const char *text, bool checked,
                         idunn_callback_t on_choose, void *data);
void idunn_menu_add_separator(idunn_menu_t *menu);
```

an item with a check that a choice turns on and off

### idunn\_menu\_add\_sub, idunn\_menu\_set\_disabled, idunn\_menu\_checked, idunn\_menu\_set\_checked

```c
int idunn_menu_add_sub(idunn_menu_t *menu, const char *icon, const char *text, idunn_menu_t *sub);
void idunn_menu_set_disabled(idunn_menu_t *menu, int item, bool disabled);
bool idunn_menu_checked(const idunn_menu_t *menu, int item);
void idunn_menu_set_checked(idunn_menu_t *menu, int item, bool checked);
```

an item that opens a sub-menu (the menu owns it)

### idunn\_menu\_set\_min\_width

```c
void idunn_menu_set_min_width(idunn_menu_t *menu, int32_t width);
```

the least width of the menu, in design pixels (160 by default)

### idunn\_menu\_popup

```c
void idunn_menu_popup(idunn_menu_t *menu, idunn_widget_t *owner);
```

open the menu under a widget (a button's on\_click does it)

### idunn\_menu\_popup\_at\_pointer

```c
void idunn_menu_popup_at_pointer(idunn_menu_t *menu, idunn_widget_t *owner);
```

open the menu where the last right click was (a context menu)

### idunn\_select, idunn\_select\_chosen, idunn\_select\_choose, idunn\_select\_set\_placeholder

```c
idunn_widget_t *idunn_select(const char *const *choices, int count, int chosen,
                             idunn_callback_t on_change, void *data);
int idunn_select_chosen(const idunn_widget_t *select);
void idunn_select_choose(idunn_widget_t *select, int chosen);
void idunn_select_set_placeholder(idunn_widget_t *select, const char *text);
```

a select: a field with the chosen value; it opens a menu of the choices.

- `chosen`: the index of the chosen one, or -1 (the placeholder shows).

### idunn\_set\_tooltip

```c
void idunn_set_tooltip(idunn_widget_t *w, const char *text);
```

a tooltip: shown over the widget after 600 ms under the pointer (NULL: none)

### idunn\_popover

```c
void idunn_popover(idunn_widget_t *owner, idunn_widget_t *content);
```

a popover: content in a card under owner; a press outside it or Escape
closes it. the popover owns content. it is 288 wide, or as wide as
content when content has a larger width (idunn\_set\_size).

### idunn\_popover\_close

```c
void idunn_popover_close(idunn_widget_t *inside);
```

close the popover a widget is in

### idunn\_file\_done\_t

```c
typedef void (*idunn_file_done_t)(const char *path, void *data);
```

the path chosen in a file dialog (the dialog is closed already)

### idunn\_file\_dialog

```c
void idunn_file_dialog(idunn_widget_t *owner, idunn_file_mode_t mode, const char *title,
                       const char *path, idunn_file_done_t done, void *data);
```

a dialog to choose a file: the folder's items as a list, a button to
the folder above, and to save, a field with the name. nothing is
called when it is cancelled.

- `path`: the folder to start in, or (to save) the whole path of the file, whose name is then in the field.

### idunn\_dialog, idunn\_dialog\_content

```c
idunn_widget_t *idunn_dialog(const char *title, const char *description);
idunn_widget_t *idunn_dialog_content(idunn_widget_t *dialog);
```

a dialog: a card with a title and a description (or NULL), in the middle
of the window over the overlay colour. add content to
idunn\_dialog\_content, then the buttons; a button closes the dialog and
then calls its on\_click (with the dialog). Escape closes it too.

### idunn\_dialog\_add\_button

```c
idunn_widget_t *idunn_dialog_add_button(idunn_widget_t *dialog, const char *text,
                                        idunn_variant_t variant, idunn_callback_t on_click,
                                        void *data);
```

a button at the bottom right: Cancel as outline, the action primary or destructive

### idunn\_dialog\_open, idunn\_dialog\_close

```c
void idunn_dialog_open(idunn_widget_t *owner, idunn_widget_t *dialog);
void idunn_dialog_close(idunn_widget_t *dialog);
```

show a dialog over the window of owner; the window owns it then

### idunn\_field

```c
idunn_widget_t *idunn_field(const char *label, idunn_widget_t *control, const char *help);
```

a form row: a label above the control and help text below it (or NULL).
the field owns the control.

### idunn\_field\_set\_error

```c
void idunn_field_set_error(idunn_widget_t *field, const char *message);
```

show an error: the message in red under the control, and the control's
border and ring red. NULL takes it away (the help text comes back).

### idunn\_context\_new, idunn\_context\_free, idunn\_context\_set\_theme

```c
idunn_context_t *idunn_context_new(const idunn_theme_t *theme, const char *font_dir,
                                   const char *icon_pack);
void idunn_context_free(idunn_context_t *ctx);
void idunn_context_set_theme(idunn_context_t *ctx, const idunn_theme_t *theme);
```

fonts and icons for a theme.

- `font_dir`: the directory with inter/ and jetbrains-mono/.
- `icon_pack`: the Lucide pack, or NULL (no icons).

Returns: the context, or NULL (no fonts).

### idunn\_window\_offscreen

```c
idunn_window_t *idunn_window_offscreen(idunn_context_t *ctx, int32_t width, int32_t height);
```

a window that draws into its own buffer, of that many pixels.

### idunn\_window\_resize

```c
void idunn_window_resize(idunn_window_t *win, int32_t width, int32_t height);
```

a new size for the window's buffer (the window lays out again).

### idunn\_window\_draw

```c
gfx_rect_t idunn_window_draw(idunn_window_t *win);
```

lay out and draw what changed.

Returns: the part of the buffer that changed (empty: none).

### idunn\_window\_invalidate

```c
void idunn_window_invalidate(idunn_window_t *win);
```

the whole window must be drawn again (after a theme change).

### idunn\_window\_pointer

```c
void idunn_window_pointer(idunn_window_t *win, idunn_pointer_t kind, int32_t x, int32_t y,
                          uint32_t mods);
```

the pointer moved, or the left button went down or up, at (x, y),
with these modifier keys held (Shift+click extends a selection).

### idunn\_window\_scroll

```c
void idunn_window_scroll(idunn_window_t *win, int32_t x, int32_t y, int32_t steps);
```

the wheel turned over (x, y): steps, up is positive.

### idunn\_window\_paste

```c
void idunn_window_paste(idunn_window_t *win, const char *text, size_t length);
```

the clipboard text a widget asked for came (from the window server).

### idunn\_window\_next\_wake

```c
uint32_t idunn_window_next_wake(const idunn_window_t *win);
```

the next time (ms) the window must be ticked for a timer, or 0 for none.

### idunn\_window\_key

```c
void idunn_window_key(idunn_window_t *win, uint16_t code, uint32_t mods, const char *text);
```

a key press (or repeat).

- `code`: a Linux key code (KEY\_ in sys/input.h).
- `text`: the characters it types (UTF-8), or "".

### idunn\_window\_tick

```c
bool idunn_window_tick(idunn_window_t *win, uint32_t now);
```

the time now in ms, for transitions.

Returns: true while a transition runs (draw again in a frame).

### idunn\_window\_cursor

```c
uint32_t idunn_window_cursor(const idunn_window_t *win);
```

the pointer shape for where the pointer is (IDUNN\_CURSOR\_).

### idunn\_window\_focus

```c
idunn_widget_t *idunn_window_focus(const idunn_window_t *win);
```

the widget with the keyboard focus, or NULL.

### idunn\_input\_t

```c
typedef struct
{
    idunn_input_type_t type;
    uint16_t code;    // KEY: the Linux key code
    uint32_t mods;    // KEY, PRESS: IDUNN_MOD_
    const char *text; // KEY: the characters it types (UTF-8), or ""; PASTE: the text
    size_t length;    // PASTE
    int32_t x;
    int32_t y;
    int clicks;
    int32_t steps;
} idunn_input_t;
```

input for a canvas. x and y are from the canvas's top left corner.

### idunn\_canvas\_hooks\_t

```c
typedef struct
{
    /**
     * draw the canvas, which is at rect in the window. only the part in
     * buf->clip must be drawn (the rest is still right).
     */
    void (*draw)(idunn_widget_t *canvas, gfx_buffer_t *buf, gfx_rect_t rect, void *data);
    /** @return true if it used the input (else it goes to the parents) */
    bool (*input)(idunn_widget_t *canvas, const idunn_input_t *in, void *data);
    /** the canvas has a new size (also the first time it is laid out) */
    void (*resize)(idunn_widget_t *canvas, int32_t width, int32_t height, void *data);
    /**
     * before a frame is drawn, while the window still holds the last
     * frame: move pixels (idunn_canvas_move) and say what to draw again.
     */
    void (*frame)(idunn_widget_t *canvas, void *data);
} idunn_canvas_hooks_t;
```

what a canvas does; each hook can be NULL.

### draw

```c
    void (*draw)(idunn_widget_t *canvas, gfx_buffer_t *buf, gfx_rect_t rect, void *data);
```

draw the canvas, which is at rect in the window. only the part in
buf-&gt;clip must be drawn (the rest is still right).

### input

```c
    bool (*input)(idunn_widget_t *canvas, const idunn_input_t *in, void *data);
```

Returns: true if it used the input (else it goes to the parents)

### resize

```c
    void (*resize)(idunn_widget_t *canvas, int32_t width, int32_t height, void *data);
```

the canvas has a new size (also the first time it is laid out)

### frame, idunn\_canvas\_hooks\_t

```c
    void (*frame)(idunn_widget_t *canvas, void *data);
} idunn_canvas_hooks_t;
```

before a frame is drawn, while the window still holds the last
frame: move pixels (idunn\_canvas\_move) and say what to draw again.

### idunn\_canvas

```c
idunn_widget_t *idunn_canvas(const idunn_canvas_hooks_t *hooks, void *data);
```

a widget the program draws itself: a terminal, a game. it takes the
keyboard (Tab too), the pointer and the wheel. it is as large as its
box lets it grow, or idunn\_set\_size.

### idunn\_canvas\_damage

```c
void idunn_canvas_damage(idunn_widget_t *canvas, gfx_rect_t r);
```

draw a rect of the canvas again (canvas coordinates)

### idunn\_canvas\_update

```c
void idunn_canvas_update(idunn_widget_t *canvas);
```

a frame soon, so the frame hook runs (new data came)

### idunn\_canvas\_move

```c
void idunn_canvas_move(idunn_widget_t *canvas, gfx_rect_t area, int32_t dy);
```

in the frame hook: move the pixels of area up (dy &lt; 0) or down; the
strip that comes into view is damaged.

### idunn\_canvas\_copy, idunn\_canvas\_paste

```c
void idunn_canvas_copy(idunn_widget_t *canvas, const char *text, size_t length);
void idunn_canvas_paste(idunn_widget_t *canvas);
```

put text on the clipboard; ask for its text (an IDUNN\_INPUT\_PASTE comes)

### idunn\_canvas\_wake

```c
void idunn_canvas_wake(idunn_widget_t *canvas, uint32_t ms);
```

an IDUNN\_INPUT\_TIMER in ms (one at a time; a new call moves it)

### idunn\_canvas\_set\_cursor

```c
void idunn_canvas_set_cursor(idunn_widget_t *canvas, uint32_t shape);
```

the pointer shape over the canvas (IDUNN\_CURSOR\_)

### idunn\_set\_focus

```c
void idunn_set_focus(idunn_widget_t *w);
```

give the keyboard to a widget in a window (a canvas at the start).

### idunn\_widget\_rect

```c
gfx_rect_t idunn_widget_rect(const idunn_widget_t *w);
```

where a widget is in its window, after layout.

## &lt;idunn_theme.h&gt;

Source: [`src/lib/idunn/include/idunn_theme.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/idunn/include/idunn_theme.h)

idunn: the design tokens (colours, sizes, text styles, shadows).

the tokens are the only place colours and sizes are written: widgets
and heimdalld use token names, never numbers. the values follow the
agreed look (plans/idunn-ui-plan.md, section 2), shadcn's "zinc" theme
with the user's accent colour.

the theme file (/etc/ginnos/theme.conf and ~/.config/ginnos/theme.conf)
has these keys in \[theme]: mode (light or dark), accent (rose, blue,
green, orange, violet, neutral, or #rrggbb), font\_size (13 to 15; all
text styles follow), scale (1 or 2), motion (true or false) and radius
(4 to 12; the small and large radii follow). the sections \[light] and
\[dark] can change any colour token for that mode, by its name:
"border = #ffffff1a".

### idunn\_theme\_default

```c
void idunn_theme_default(idunn_theme_t *theme);
```

the default theme: dark, rose, 13 px text, 6 px radius.

### idunn\_theme\_from\_conf

```c
void idunn_theme_from_conf(idunn_theme_t *theme, const conf_t *conf);
```

a theme from a theme file (or from text, for tests); missing or bad
values keep their defaults.

### idunn\_theme\_load

```c
void idunn_theme_load(idunn_theme_t *theme);
```

read the user's theme (conf\_open("theme")).

### IDUNN\_HIDPI\_WIDTH

```c
#define IDUNN_HIDPI_WIDTH 2400
```

screens at least this wide get scale 2 when the scale is auto

### idunn\_theme\_fit

```c
bool idunn_theme_fit(idunn_theme_t *theme, int32_t screen_width);
```

set the scale for a screen when the theme's scale is auto.

Returns: true if the scale changed.

### idunn\_color\_name

```c
const char *idunn_color_name(idunn_color_t which);
```

the name of a colour token in the theme file ("muted-foreground"), or
NULL.

### idunn\_px

```c
static inline int32_t idunn_px(const idunn_theme_t *theme, int32_t px);
```

a size of the design at the theme's scale: idunn\_px(theme, 32).

### idunn\_radius

```c
int32_t idunn_radius(const idunn_theme_t *theme);
```

buttons, inputs, list rows

### idunn\_radius\_sm

```c
int32_t idunn_radius_sm(const idunn_theme_t *theme);
```

check boxes, small items in menus: half the radius

### idunn\_radius\_lg

```c
int32_t idunn_radius_lg(const idunn_theme_t *theme);
```

cards, dialogs, popovers, windows: the radius + 4

### idunn\_text\_style

```c
idunn_text_style_t idunn_text_style(const idunn_theme_t *theme, idunn_text_t which);
```

a text style at the theme's font size and scale.

### idunn\_shadow

```c
int idunn_shadow(const idunn_theme_t *theme, idunn_shadow_t which,
                 gfx_shadow_t layers[IDUNN_SHADOW_LAYERS]);
```

the layers of a shadow, in the order to draw them (the CSS list from
its end), at the theme's scale.

Returns: the number of layers (at most IDUNN\_SHADOW\_LAYERS).

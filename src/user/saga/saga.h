#pragma once

/**
 * @file saga.h
 * @brief Saga, the text editor: the parts its files share.
 *
 * the files:
 *   main.c     the window, the toolbar, the status line, the keys
 *   buffer.c   the text as lines, undo and redo (plain C, host tested)
 *   editor.c   the editing canvas: drawing, the caret, the selection
 *   find.c     the find and replace bar
 *   file.c     open, save, save as, and "save the changes?"
 */

#include "buffer.h"

#include <idunn.h>
#include <stdbool.h>
#include <stdint.h>

#define PATH_LEN  1024
#define TAB_WIDTH 4

typedef struct
{
    idunn_app_t *app;
    idunn_window_t *win;
    buffer_t buffer;
    char path[PATH_LEN]; // "" for a new text
    idunn_widget_t *canvas;
    idunn_widget_t *status_place;
    idunn_widget_t *status_kind;
} saga_t;

extern saga_t S;

// editor.c

idunn_widget_t *editor_new(void);
/** the text changed from outside (a file was read): caret to the start */
void editor_reset(void);
void editor_select(pos_t start, pos_t end);
/** the selection, ordered; false when nothing is selected */
bool editor_selection(pos_t *start, pos_t *end);
pos_t editor_caret(void);
/** replace the selection (or insert at the caret) */
void editor_replace(const char *text, size_t length);
void editor_undo(void);
void editor_redo(void);
void editor_focus(void);
/** the text size: +1, -1, or 0 for the normal one */
void editor_zoom(int step);
/** the theme changed: new colours, maybe another scale */
void editor_theme_changed(void);

// main.c

/** the caret moved or the text changed: the title and the status line */
void saga_changed(void);
void show_message(const char *title, const char *text);

// find.c

idunn_widget_t *find_bar_new(void);
void find_show(bool replace);
void find_hide(void);
/** the next (or previous) match of the find field */
void find_next(bool backward);

// file.c

/** read a file into the buffer; false (with a message) when it cannot */
bool file_open(const char *path);
/** write the buffer to S.path (save as when there is none) */
void file_save(void);
void file_save_as(void);
/** ask for a path, then open it (after asking about unsaved changes) */
void file_open_dialog(void);
void file_new(void);
/** the close button: true when the window can close now */
bool file_close_window(idunn_window_t *win, void *data);

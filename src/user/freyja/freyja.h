#pragma once

/**
 * @file freyja.h
 * @brief Freyja, the image viewer: the parts its files share.
 *
 * the files:
 *   main.c     the window, the toolbar, the status line, the keys, loading
 *   view.c     the canvas: fit, zoom, pan
 *   picture.c  the picture and its smaller copies (plain C, host tested)
 *   folder.c   the other pictures of the folder (plain C, host tested)
 */

#include "folder.h"
#include "picture.h"

#include <idunn.h>

#define PATH_LEN 1024

typedef struct
{
    idunn_app_t *app;
    idunn_window_t *win;
    picture_t picture;
    folder_t folder;
    char path[PATH_LEN]; // "" when no picture is open
    char error[160];     // why the last picture did not open, or ""
    idunn_widget_t *canvas;
    idunn_widget_t *zoom_label;
    idunn_widget_t *status_name;
    idunn_widget_t *status_size;
} freyja_t;

extern freyja_t F;

// view.c

idunn_widget_t *view_new(void);
/** a new picture: fit it in the window */
void view_reset(void);
/** fit the picture in the window (it is not made larger than 100 %) */
void view_fit(void);
/** zoom: a factor about the middle of the window, or 1.0 for 100 % */
void view_zoom_by(double factor);
void view_zoom_to(double zoom);
double view_zoom(void);
bool view_fitted(void);
void view_redraw(void);

// main.c

/** the zoom or the picture changed: the zoom label and the status line */
void freyja_changed(void);
void freyja_step(int step);

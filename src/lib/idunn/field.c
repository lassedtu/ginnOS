/**
 * @file field.c
 * @brief idunn: a field: a label above a control, and help text below
 *        it, 6 pixels apart, as the usual form row. with an error, the help
 *        text is the message, in red, and the control is red too.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define GAP 6

typedef struct
{
    idunn_widget_t *label;
    idunn_widget_t *control;
    idunn_widget_t *help; // NULL without help and error
    char *help_text;
} field_t;

static void field_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    int32_t gap = idunn_px(idunn_theme(w), GAP);
    *width = 0;
    *height = 0;
    for (int i = 0; i < w->child_count; i++)
    {
        int32_t cw;
        int32_t ch;
        idunn_measure(w->children[i], max_width, &cw, &ch);
        *width = cw > *width ? cw : *width;
        *height += ch + (i ? gap : 0);
    }
}

static void field_layout(idunn_widget_t *w)
{
    int32_t gap = idunn_px(idunn_theme(w), GAP);
    int32_t y = w->rect.y;
    for (int i = 0; i < w->child_count; i++)
    {
        int32_t cw;
        int32_t ch;
        idunn_measure(w->children[i], w->rect.w, &cw, &ch);
        idunn_place(w->children[i], gfx_rect(w->rect.x, y, w->rect.w, ch));
        y += ch + gap;
    }
}

static void field_free(idunn_widget_t *w)
{
    field_t *f = w->data;
    free(f->help_text);
    free(f);
}

static const idunn_class_t field_class = {
    .name = "field",
    .measure = field_measure,
    .layout = field_layout,
    .free = field_free,
};

static idunn_widget_t *small_text(const char *text)
{
    idunn_widget_t *help = idunn_label(text, IDUNN_TEXT_SMALL);
    idunn_label_set_color(help, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_label_set_wrap(help, true);
    return help;
}

idunn_widget_t *idunn_field(const char *label, idunn_widget_t *control, const char *help)
{
    field_t *f = calloc(1, sizeof(*f));
    idunn_widget_t *w = f ? idunn_widget_new(&field_class, f, IDUNN_SHRINKS) : NULL;
    if (!w)
    {
        free(f);
        idunn_widget_free(control);
        return NULL;
    }
    f->label = idunn_add(w, idunn_label(label ? label : "", IDUNN_TEXT_LABEL));
    f->control = idunn_add(w, control);
    if (help)
    {
        f->help_text = strdup(help);
        f->help = idunn_add(w, small_text(help));
    }
    return w;
}

void idunn_field_set_error(idunn_widget_t *field, const char *message)
{
    if (!field || field->cls != &field_class)
        return;
    field_t *f = field->data;
    idunn_set_error(f->control, message != NULL);
    const char *text = message ? message : f->help_text;
    if (!text)
    {
        // no help to go back to: the line goes away
        idunn_widget_free(f->help);
        f->help = NULL;
        return;
    }
    if (!f->help)
        f->help = idunn_add(field, small_text(text));
    idunn_label_set_text(f->help, text);
    idunn_label_set_color(f->help, message ? IDUNN_COLOR_DESTRUCTIVE : IDUNN_COLOR_MUTED_FOREGROUND);
}

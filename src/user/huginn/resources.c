/**
 * @file resources.c
 * @brief Huginn: the Resources tab: the CPU over the last minute, the
 *        memory in use, and how long the computer has been up.
 */

#include "huginn.h"

#include <stdio.h>

#define GRAPH_HEIGHT 140 // design pixels

static struct
{
    idunn_widget_t *cpu_value;
    idunn_widget_t *graph;
    int32_t graph_width;
    int32_t graph_height;
    idunn_widget_t *memory_value;
    idunn_widget_t *memory_bar;
    idunn_widget_t *uptime;
    idunn_widget_t *processes;
} R;

static gfx_color_t token(idunn_color_t c)
{
    const idunn_theme_t *t = idunn_app_theme(H.app);
    return t->colors[t->mode][c];
}

/**
 * the graph: a column for each second, newest at the right, filled in a
 * faint accent under a line in the accent; lines at 25, 50 and 75 %.
 */
static void draw_graph(idunn_widget_t *canvas, gfx_buffer_t *buf, gfx_rect_t rect, void *data)
{
    (void)canvas;
    (void)data;
    const idunn_theme_t *t = idunn_app_theme(H.app);
    gfx_fill_rect(buf, rect, token(IDUNN_COLOR_MUTED));
    for (int q = 1; q < 4; q++)
        gfx_fill_rect(buf, gfx_rect(rect.x, rect.y + rect.h * q / 4, rect.w, t->scale),
                      token(IDUNN_COLOR_BORDER));
    gfx_color_t accent = token(IDUNN_COLOR_ACCENT);
    gfx_color_t fill = gfx_fade(accent, 60);
    int32_t line = 2 * t->scale;
    double step = rect.w / (double)(HISTORY - 1);
    int32_t last_x = 0;
    int32_t last_y = 0;
    for (int i = 0; i < H.cpu_count; i++)
    {
        // the newest at the right edge
        int slot = HISTORY - H.cpu_count + i;
        int32_t x = rect.x + (int32_t)(slot * step);
        int32_t y = rect.y + rect.h - (int32_t)(H.cpu[i] / 100.0 * (rect.h - line));
        if (i > 0)
        {
            for (int32_t px = last_x; px < x; px++)
            {
                int32_t py = last_y + (int32_t)((y - last_y) * (px - last_x) / (double)(x - last_x));
                gfx_fill_rect(buf, gfx_rect(px, py, 1, rect.y + rect.h - py), fill);
                gfx_fill_rect(buf, gfx_rect(px, py - line / 2, 1, line), accent);
            }
        }
        last_x = x;
        last_y = y;
    }
}

static void graph_resized(idunn_widget_t *canvas, int32_t width, int32_t height, void *data)
{
    (void)canvas;
    (void)data;
    R.graph_width = width;
    R.graph_height = height;
}

static idunn_widget_t *value_label(idunn_widget_t *row)
{
    idunn_widget_t *label = idunn_add(row, idunn_label(" ", IDUNN_TEXT_BODY));
    idunn_label_set_color(label, IDUNN_COLOR_MUTED_FOREGROUND);
    return label;
}

/** a card's head: its title at the left, its value at the right */
static idunn_widget_t *card_head(idunn_widget_t *card, const char *title)
{
    idunn_widget_t *row = idunn_add(card, idunn_row(2, 0));
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_SPACE_BETWEEN);
    idunn_add(row, idunn_label(title, IDUNN_TEXT_HEADING));
    return value_label(row);
}

void resources_update(void)
{
    char text[96];
    snprintf(text, sizeof(text), "%.0f %%", H.cpu_count ? H.cpu[H.cpu_count - 1] : 0.0);
    idunn_label_set_text(R.cpu_value, text);
    idunn_canvas_damage(R.graph, gfx_rect(0, 0, R.graph_width, R.graph_height));

    uint32_t total = H.now.mem_total_kb;
    uint32_t used = total > H.now.mem_free_kb ? total - H.now.mem_free_kb : 0;
    char used_text[24];
    char total_text[24];
    format_kb(used, used_text, sizeof(used_text));
    format_kb(total, total_text, sizeof(total_text));
    snprintf(text, sizeof(text), "%s of %s", used_text, total_text);
    idunn_label_set_text(R.memory_value, text);
    idunn_progress_set(R.memory_bar,
                       total ? (int32_t)((uint64_t)used * IDUNN_PROGRESS_MAX / total) : 0);

    long seconds = (long)H.now.uptime;
    long days = seconds / 86400;
    long hours = seconds / 3600 % 24;
    long minutes = seconds / 60 % 60;
    if (days)
        snprintf(text, sizeof(text), "%ld d %ld h %ld min", days, hours, minutes);
    else if (hours)
        snprintf(text, sizeof(text), "%ld h %ld min", hours, minutes);
    else
        snprintf(text, sizeof(text), "%ld min %ld s", minutes, seconds % 60);
    idunn_label_set_text(R.uptime, text);
    snprintf(text, sizeof(text), "%d", H.now.count);
    idunn_label_set_text(R.processes, text);
}

/** a line of the System card: a name at the left, a value at the right */
static idunn_widget_t *fact(idunn_widget_t *card, const char *name)
{
    idunn_widget_t *row = idunn_add(card, idunn_row(2, 0));
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_SPACE_BETWEEN);
    idunn_add(row, idunn_label(name, IDUNN_TEXT_BODY));
    return value_label(row);
}

idunn_widget_t *resources_page(void)
{
    idunn_widget_t *page = idunn_column(4, 0);
    idunn_box_set_padding(page, 3, 0);

    idunn_widget_t *cpu = idunn_add(page, idunn_card(3));
    R.cpu_value = card_head(cpu, "CPU");
    static const idunn_canvas_hooks_t hooks = {draw_graph, NULL, graph_resized, NULL};
    R.graph = idunn_add(cpu, idunn_canvas(&hooks, NULL));
    idunn_set_size(R.graph, 0, GRAPH_HEIGHT);
    idunn_widget_t *note = idunn_add(cpu, idunn_label("The last minute", IDUNN_TEXT_SMALL));
    idunn_label_set_color(note, IDUNN_COLOR_MUTED_FOREGROUND);

    idunn_widget_t *memory = idunn_add(page, idunn_card(3));
    R.memory_value = card_head(memory, "Memory");
    R.memory_bar = idunn_add(memory, idunn_progress());

    idunn_widget_t *system = idunn_add(page, idunn_card(2));
    idunn_add(system, idunn_label("System", IDUNN_TEXT_HEADING));
    R.uptime = fact(system, "Up for");
    R.processes = fact(system, "Processes");
    return idunn_scroll_view(page);
}

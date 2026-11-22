/**
 * @file common.c
 * @brief settings: the helpers every page uses.
 */

#include "settings.h"

#include <conf.h>
#include <stdio.h>
#include <string.h>

idunn_app_t *settings_app;
heim_conn_t *settings_conn;

idunn_widget_t *muted(const char *text, idunn_text_t style)
{
    idunn_widget_t *label = idunn_label(text, style);
    idunn_label_set_color(label, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_label_set_wrap(label, true);
    return label;
}

idunn_widget_t *page_card(idunn_widget_t *page, const char *title, const char *text)
{
    idunn_widget_t *card = idunn_add(page, idunn_card(4));
    idunn_card_header(card, title, text);
    return card;
}

void save_setting(const char *file, const char *section_name, const char *key, const char *value)
{
    conf_t *conf = conf_open(file);
    if (!conf)
        return;
    conf_set(conf, section_name, key, value);
    if (conf_save(conf) != 0)
        fprintf(stderr, "settings: cannot save %s.conf\n", file);
    conf_free(conf);
    heim_settings_changed(settings_conn, file);
}

void read_setting(const char *file, const char *section_name, const char *key,
                    const char *fallback, char *out, size_t size)
{
    conf_t *conf = conf_open(file);
    const char *value = conf ? conf_get(conf, section_name, key, fallback) : fallback;
    snprintf(out, size, "%s", value ? value : "");
    conf_free(conf);
}

int index_of(const char *const *list, int count, const char *value)
{
    for (int i = 0; i < count; i++)
        if (strcmp(list[i], value) == 0)
            return i;
    return -1;
}

bool read_field(const char *path, const char *key, char *out, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[256];
    bool found = false;
    size_t n = strlen(key);
    while (!found && fgets(line, sizeof(line), f))
    {
        if (strncmp(line, key, n) != 0)
            continue;
        const char *v = line + n;
        while (*v == ' ' || *v == '\t' || *v == ':')
            v++;
        snprintf(out, size, "%s", v);
        out[strcspn(out, "\n")] = '\0';
        found = true;
    }
    fclose(f);
    return found;
}

void fact(idunn_widget_t *card, const char *name, const char *value)
{
    idunn_widget_t *row = idunn_add(card, idunn_row(4, 0));
    idunn_widget_t *label = idunn_add(row, muted(name, IDUNN_TEXT_BODY));
    idunn_set_size(label, 120, 0);
    idunn_grow(idunn_add(row, idunn_label(value, IDUNN_TEXT_BODY)), 1);
}

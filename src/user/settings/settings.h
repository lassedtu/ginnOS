#pragma once

/**
 * @file settings.h
 * @brief settings: the parts the pages share.
 *
 * main.c makes the window and the list of pages; each page is a file of
 * its own (appearance.c, display.c, keyboard.c, sound.c, terminal.c, about.c) with
 * one function that builds it. common.c has the helpers below.
 */

#include <heimdall.h>
#include <idunn.h>
#include <stdbool.h>
#include <stddef.h>

#define CHOICES_MAX 32 // the most choices a list of a page shows
#define NAME_LEN    64 // the longest path or name of a choice

extern idunn_app_t *settings_app;
extern heim_conn_t *settings_conn;

idunn_widget_t *appearance_page(void);
idunn_widget_t *keyboard_page(void);
idunn_widget_t *display_page(void);
idunn_widget_t *sound_page(void);
idunn_widget_t *terminal_page(void);
idunn_widget_t *about_page(void);

/** a label in the muted colour, wrapped */
idunn_widget_t *muted(const char *text, idunn_text_t style);

/** a card in page with a title and a line about it (text can be NULL) */
idunn_widget_t *page_card(idunn_widget_t *page, const char *title, const char *text);

/** write one value into a settings file, and tell every program */
void save_setting(const char *file, const char *section_name, const char *key,
                  const char *value);

/** a value of a settings file now (or fallback), into out */
void read_setting(const char *file, const char *section_name, const char *key,
                  const char *fallback, char *out, size_t size);

/** the index of value in list, or -1 */
int index_of(const char *const *list, int count, const char *value);

/** the rest of the first line of a file that starts with key, or false */
bool read_field(const char *path, const char *key, char *out, size_t size);

/** a row of facts: the name muted, the value after it */
void fact(idunn_widget_t *card, const char *name, const char *value);

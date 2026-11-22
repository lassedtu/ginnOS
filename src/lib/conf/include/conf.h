#pragma once

/**
 * @file conf.h
 * @brief settings files: sections of "key = value" lines.
 *
 * a settings file looks like this:
 *
 *     # the look of the desktop
 *     [theme]
 *     mode = dark
 *     accent = #3b82f6
 *     font_size = 13
 *
 * - a line that starts with '#' or ';' is a comment. there are no comments
 *   after a value, so "#3b82f6" is a value.
 * - keys before the first [section] are in the section "".
 * - spaces around the key and the value are removed. a value in double
 *   quotes keeps its spaces ("  x  "); the quotes are removed.
 * - if a key is in a section two times, the last one is used.
 * - a bad line (no '=', no key, a [section without ']') is kept as it is
 *   when the file is written again, and gives a warning on stderr.
 *
 * a conf_t has two layers: the system defaults (/etc/ginnos/<name>.conf,
 * read only) and the user's values (~/.config/ginnos/<name>.conf). a get
 * looks at the user's values first. a set changes only the user's values,
 * and conf_save writes only the user's file. comments, blank lines and the
 * order of the lines stay as they were.
 */

#include <stdbool.h>
#include <stdint.h>

#define CONF_SYSTEM_DIR "/etc/ginnos"
#define CONF_USER_DIR   ".config/ginnos" // in the home directory

typedef struct conf conf_t;

/**
 * open the settings called name: /etc/ginnos/<name>.conf and
 * ~/.config/ginnos/<name>.conf. a file that is not there is empty.
 * @param name letters, digits, '-' and '_' only.
 * @return the settings, or NULL (a bad name, or no memory).
 */
conf_t *conf_open(const char *name);

/**
 * open two files. user_path is the file conf_save writes.
 * @param system_path may be NULL (no defaults).
 * @param user_path may be NULL (conf_save then fails).
 */
conf_t *conf_load(const char *system_path, const char *user_path);

/**
 * settings from text in memory (for tests). conf_save fails on them.
 * @param system_text the defaults, may be NULL.
 * @param user_text the user's values, may be NULL.
 */
conf_t *conf_from_text(const char *system_text, const char *user_text);

void conf_free(conf_t *conf);

/**
 * a value.
 * @return the user's value, else the system value, else fallback. the
 *         string is valid until the next set, unset or free.
 */
const char *conf_get(const conf_t *conf, const char *section, const char *key,
                     const char *fallback);

/**
 * a whole number: decimal, or hexadecimal with 0x.
 * @return fallback if the key is not there or is not a number.
 */
long conf_get_int(const conf_t *conf, const char *section, const char *key, long fallback);

/**
 * true, yes, on, 1 / false, no, off, 0 (any letter case).
 * @return fallback if the key is not there or is not one of these.
 */
bool conf_get_bool(const conf_t *conf, const char *section, const char *key, bool fallback);

/**
 * a colour: #rrggbb or #rrggbbaa.
 * @return 0xAARRGGBB, not premultiplied (gfx_rgba makes a gfx colour of
 *         it); alpha is 0xFF for #rrggbb. fallback if the key is not there
 *         or is not a colour.
 */
uint32_t conf_get_color(const conf_t *conf, const char *section, const char *key,
                        uint32_t fallback);

/**
 * set a user value. the line is changed where it is; a new key goes after
 * the last line of its section, and a new section goes at the end.
 * @param section a name without '[', ']' or a line break.
 * @param key a name without '=', a line break, or a '#' or ';' or '['
 *        at its start.
 * @param value text without a line break.
 * @return 0, or -1 (a bad name or value, or no memory).
 */
int conf_set(conf_t *conf, const char *section, const char *key, const char *value);
int conf_set_int(conf_t *conf, const char *section, const char *key, long value);
int conf_set_bool(conf_t *conf, const char *section, const char *key, bool value);
int conf_set_color(conf_t *conf, const char *section, const char *key, uint32_t argb);

/**
 * remove the user's value, so the system value is used again.
 * @return 0 (also when there was no user value), or -1 (bad arguments).
 */
int conf_unset(conf_t *conf, const char *section, const char *key);

/**
 * write the user's file: to a new file, fsync, then rename over the old
 * one, so a crash leaves the old file or the new one. the directory is
 * made if it is not there.
 * @return 0, or -1 with errno set.
 */
int conf_save(const conf_t *conf);

/**
 * the user's file as text, as conf_save writes it (for tests).
 * @return a string to free, or NULL (no memory).
 */
char *conf_user_text(const conf_t *conf);

/**
 * how many bad lines the two files had.
 */
int conf_bad_lines(const conf_t *conf);

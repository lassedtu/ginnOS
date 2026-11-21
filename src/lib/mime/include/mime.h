#pragma once

/**
 * @file mime.h
 * @brief file types: the type name and the icon of a file.
 *
 * the table is /usr/share/ginnos/mime.conf (its format is described in
 * the file). a file is matched by its name's extension, then by its first
 * bytes, then as text.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIME_CONF "/usr/share/ginnos/mime.conf"

// how many first bytes of a file the magic rules can look at
#define MIME_HEAD_SIZE 64

typedef struct
{
    const char *type; // "text/plain"
    const char *icon; // a Tela icon name, "text-x-generic"
} mime_type_t;

typedef struct mime_table mime_table_t;

/**
 * read a table file (MIME_CONF for the system one).
 * @return the table, or NULL (no file). bad lines are skipped.
 */
mime_table_t *mime_open(const char *path);

/**
 * a table from text in memory (for tests).
 */
mime_table_t *mime_from_text(const char *text);

void mime_close(mime_table_t *table);

/**
 * the type of a file from its name and its first bytes.
 * @param name the file name (a path is fine), or NULL.
 * @param head the first bytes of the file, or NULL.
 * @return the type; never NULL (application/octet-stream when nothing
 *         matches). it stays valid until the table is closed.
 */
const mime_type_t *mime_lookup(const mime_table_t *table, const char *name,
                               const uint8_t *head, size_t head_len);

/**
 * the type of a file on disk: inode/directory for a directory, else by
 * its name and first bytes.
 */
const mime_type_t *mime_lookup_file(const mime_table_t *table, const char *path);

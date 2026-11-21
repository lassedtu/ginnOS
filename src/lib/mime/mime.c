/**
 * @file mime.c
 * @brief file types from /usr/share/ginnos/mime.conf.
 */

#include "mime.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONF_MAX_SIZE (256 * 1024)
#define EXT_MAX 15

typedef enum
{
    RULE_EXT,
    RULE_MAGIC,
    RULE_TEXT,
} rule_kind_t;

typedef struct
{
    rule_kind_t kind;
    uint16_t type;  // index into the types
    uint8_t length; // magic bytes
    char ext[EXT_MAX + 1];
    uint8_t magic[MIME_HEAD_SIZE];
} rule_t;

struct mime_table
{
    char *strings; // the table text, cut into the names
    mime_type_t *types;
    uint32_t type_count;
    rule_t *rules;
    uint32_t rule_count;
};

static const mime_type_t octet_stream = {"application/octet-stream", "unknown"};
static const mime_type_t directory = {"inode/directory", "folder"};

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

/**
 * the next word of a line (cut with a zero), or NULL at the end.
 */
static char *next_word(char **s)
{
    char *p = *s;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '\0')
        return NULL;
    char *word = p;
    while (*p && *p != ' ' && *p != '\t')
        p++;
    if (*p)
        *p++ = '\0';
    *s = p;
    return word;
}

static bool parse_rule(rule_t *rule, const char *word)
{
    if (word[0] == '.')
    {
        size_t len = strlen(word + 1);
        if (len == 0 || len > EXT_MAX)
            return false;
        rule->kind = RULE_EXT;
        for (size_t i = 0; i <= len; i++)
            rule->ext[i] = lower(word[1 + i]);
        return true;
    }
    if (strcmp(word, "text") == 0)
    {
        rule->kind = RULE_TEXT;
        return true;
    }
    if (strncmp(word, "magic:", 6) != 0)
        return false;
    rule->kind = RULE_MAGIC;
    rule->length = 0;
    for (const char *p = word + 6; *p; p++)
    {
        if (rule->length == MIME_HEAD_SIZE)
            return false;
        uint8_t byte = (uint8_t)*p;
        if (p[0] == '\\' && p[1] == 'x')
        {
            int hi = hex_digit(p[2]);
            int lo = hi < 0 ? -1 : hex_digit(p[3]);
            if (lo < 0)
                return false;
            byte = (uint8_t)(hi << 4 | lo);
            p += 3;
        }
        rule->magic[rule->length++] = byte;
    }
    return rule->length > 0;
}

mime_table_t *mime_from_text(const char *text)
{
    mime_table_t *t = calloc(1, sizeof(*t));
    size_t size = strlen(text);
    if (!t || !(t->strings = malloc(size + 1)))
    {
        free(t);
        return NULL;
    }
    memcpy(t->strings, text, size + 1);

    // room for every line and every word, more than is needed
    uint32_t lines = 1;
    uint32_t words = 1;
    for (size_t i = 0; i < size; i++)
    {
        lines += text[i] == '\n';
        words += text[i] == ' ' || text[i] == '\t' || text[i] == '\n';
    }
    t->types = calloc(lines, sizeof(mime_type_t));
    t->rules = calloc(words, sizeof(rule_t));
    if (!t->types || !t->rules || lines > 65535)
    {
        mime_close(t);
        return NULL;
    }

    char *line = t->strings;
    while (line)
    {
        char *end = strchr(line, '\n');
        if (end)
            *end = '\0';
        char *hash = strchr(line, '#');
        if (hash && (hash == line || hash[-1] == ' ' || hash[-1] == '\t'))
            *hash = '\0';

        char *s = line;
        char *type = next_word(&s);
        char *icon = type ? next_word(&s) : NULL;
        if (icon)
        {
            uint32_t index = t->type_count++;
            t->types[index].type = type;
            t->types[index].icon = icon;
            char *word;
            while ((word = next_word(&s)) != NULL)
            {
                rule_t *rule = &t->rules[t->rule_count];
                if (parse_rule(rule, word))
                {
                    rule->type = (uint16_t)index;
                    t->rule_count++;
                }
            }
        }
        line = end ? end + 1 : NULL;
    }
    return t;
}

mime_table_t *mime_open(const char *path)
{
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > CONF_MAX_SIZE)
    {
        if (fd >= 0)
            close(fd);
        return NULL;
    }
    size_t size = (size_t)st.st_size;
    char *text = malloc(size + 1);
    size_t done = 0;
    while (text && done < size)
    {
        ssize_t n = read(fd, text + done, size - done);
        if (n <= 0)
            break;
        done += (size_t)n;
    }
    close(fd);
    if (!text)
        return NULL;
    text[done] = '\0';
    mime_table_t *t = mime_from_text(text);
    free(text);
    return t;
}

void mime_close(mime_table_t *table)
{
    if (!table)
        return;
    free(table->strings);
    free(table->types);
    free(table->rules);
    free(table);
}

/**
 * true if the bytes look like text: no control bytes but tab, newline,
 * carriage return and form feed, and valid UTF-8 (a sequence cut at the
 * end is fine).
 */
static bool is_text(const uint8_t *s, size_t n)
{
    if (n == 0)
        return false;
    for (size_t i = 0; i < n;)
    {
        uint8_t c = s[i];
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f')
            return false;
        if (c == 0x7F)
            return false;
        size_t extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2
                                    : (c & 0xF8) == 0xF0 ? 3 : 4;
        if (extra == 4 || (c >= 0x80 && c < 0xC2))
            return false;
        for (size_t k = 1; k <= extra && i + k < n; k++)
            if ((s[i + k] & 0xC0) != 0x80)
                return false;
        i += 1 + extra;
    }
    return true;
}

const mime_type_t *mime_lookup(const mime_table_t *table, const char *name,
                               const uint8_t *head, size_t head_len)
{
    if (!table)
        return &octet_stream;

    // the extension: after the last dot of the last path part
    const char *base = name ? strrchr(name, '/') : NULL;
    base = base ? base + 1 : name;
    const char *dot = base ? strrchr(base, '.') : NULL;
    if (dot && dot != base && dot[1] && strlen(dot + 1) <= EXT_MAX)
    {
        char ext[EXT_MAX + 1];
        size_t len = strlen(dot + 1);
        for (size_t i = 0; i <= len; i++)
            ext[i] = lower(dot[1 + i]);
        for (uint32_t i = 0; i < table->rule_count; i++)
            if (table->rules[i].kind == RULE_EXT && strcmp(table->rules[i].ext, ext) == 0)
                return &table->types[table->rules[i].type];
    }

    if (!head)
        return &octet_stream;
    for (uint32_t i = 0; i < table->rule_count; i++)
    {
        const rule_t *r = &table->rules[i];
        if (r->kind == RULE_MAGIC && r->length <= head_len &&
            memcmp(r->magic, head, r->length) == 0)
            return &table->types[r->type];
    }
    if (is_text(head, head_len))
    {
        for (uint32_t i = 0; i < table->rule_count; i++)
            if (table->rules[i].kind == RULE_TEXT)
                return &table->types[table->rules[i].type];
    }
    return &octet_stream;
}

const mime_type_t *mime_lookup_file(const mime_table_t *table, const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return &directory;
    uint8_t head[MIME_HEAD_SIZE];
    ssize_t n = -1;
    int fd = open(path, O_RDONLY);
    if (fd >= 0)
    {
        n = read(fd, head, sizeof(head));
        close(fd);
    }
    return mime_lookup(table, path, n > 0 ? head : NULL, n > 0 ? (size_t)n : 0);
}

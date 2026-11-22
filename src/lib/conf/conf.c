/**
 * @file conf.c
 * @brief libconf: settings files with sections and "key = value" lines.
 *
 * each file is kept as a list of its lines. a line keeps its text, so a
 * file is written again with its comments, blank lines and bad lines as
 * they were; a set changes or adds only the one line.
 */

#include "conf.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NAME_MAX_LEN 64
#define PATH_LEN     256

typedef enum
{
    LINE_OTHER,   // blank or a comment
    LINE_SECTION, // [name]
    LINE_VALUE,   // key = value
    LINE_BAD,
} line_kind_t;

typedef struct
{
    char *text;    // the line as it is in the file
    line_kind_t kind;
    char *section; // LINE_SECTION: its name; LINE_VALUE: the section it is in
    char *key;     // LINE_VALUE only
    char *value;   // LINE_VALUE only, without spaces and quotes
} line_t;

typedef struct
{
    line_t *lines;
    int count;
    int capacity;
} doc_t;

struct conf
{
    doc_t system;
    doc_t user;
    char *user_path; // NULL: conf_save fails
    int bad_lines;
};

static char *copy_span(const char *start, size_t length)
{
    char *s = malloc(length + 1);
    if (s)
    {
        memcpy(s, start, length);
        s[length] = '\0';
    }
    return s;
}

static char *copy_string(const char *s)
{
    return copy_span(s, strlen(s));
}

/**
 * the part of [start, end) without spaces at the two ends.
 */
static void trim(const char **start, const char **end)
{
    while (*start < *end && isspace((unsigned char)**start))
        (*start)++;
    while (*end > *start && isspace((unsigned char)(*end)[-1]))
        (*end)--;
}

static void line_free(line_t *line)
{
    free(line->text);
    free(line->section);
    free(line->key);
    free(line->value);
    memset(line, 0, sizeof(*line));
}

/**
 * read one line of text into its parts.
 * @param section the section the line is in.
 * @return false if there is no memory.
 */
static bool parse_line(line_t *line, const char *text, size_t length, const char *section)
{
    memset(line, 0, sizeof(*line));
    line->text = copy_span(text, length);
    if (!line->text)
        return false;

    const char *start = line->text;
    const char *end = line->text + length;
    trim(&start, &end);

    if (start == end || *start == '#' || *start == ';')
    {
        line->kind = LINE_OTHER;
        return true;
    }
    if (*start == '[')
    {
        if (end[-1] != ']' || end - start < 2)
        {
            line->kind = LINE_BAD;
            return true;
        }
        const char *name = start + 1;
        const char *name_end = end - 1;
        trim(&name, &name_end);
        line->kind = LINE_SECTION;
        line->section = copy_span(name, (size_t)(name_end - name));
        return line->section != NULL;
    }

    const char *equals = memchr(start, '=', (size_t)(end - start));
    if (!equals)
    {
        line->kind = LINE_BAD;
        return true;
    }
    const char *key = start;
    const char *key_end = equals;
    trim(&key, &key_end);
    if (key == key_end)
    {
        line->kind = LINE_BAD;
        return true;
    }
    const char *value = equals + 1;
    const char *value_end = end;
    trim(&value, &value_end);
    if (value_end - value >= 2 && *value == '"' && value_end[-1] == '"')
    {
        value++;
        value_end--;
    }

    line->kind = LINE_VALUE;
    line->section = copy_string(section);
    line->key = copy_span(key, (size_t)(key_end - key));
    line->value = copy_span(value, (size_t)(value_end - value));
    return line->section && line->key && line->value;
}

static void doc_free(doc_t *doc)
{
    for (int i = 0; i < doc->count; i++)
        line_free(&doc->lines[i]);
    free(doc->lines);
    memset(doc, 0, sizeof(*doc));
}

/**
 * make room for one more line at index at.
 */
static line_t *doc_insert(doc_t *doc, int at)
{
    if (doc->count == doc->capacity)
    {
        int capacity = doc->capacity ? doc->capacity * 2 : 16;
        line_t *lines = realloc(doc->lines, (size_t)capacity * sizeof(line_t));
        if (!lines)
            return NULL;
        doc->lines = lines;
        doc->capacity = capacity;
    }
    memmove(&doc->lines[at + 1], &doc->lines[at], (size_t)(doc->count - at) * sizeof(line_t));
    doc->count++;
    memset(&doc->lines[at], 0, sizeof(line_t));
    return &doc->lines[at];
}

static void doc_remove(doc_t *doc, int at)
{
    line_free(&doc->lines[at]);
    memmove(&doc->lines[at], &doc->lines[at + 1], (size_t)(doc->count - at - 1) * sizeof(line_t));
    doc->count--;
}

/**
 * read text into a document.
 * @param origin the file name for warnings.
 * @return the number of bad lines, or -1 if there is no memory.
 */
static int doc_parse(doc_t *doc, const char *text, const char *origin)
{
    const char *section = "";
    int bad = 0;
    int number = 0;
    const char *p = text;
    while (*p)
    {
        const char *end = strchr(p, '\n');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        number++;
        size_t kept = length;
        if (kept > 0 && p[kept - 1] == '\r')
            kept--;

        line_t *line = doc_insert(doc, doc->count);
        if (!line || !parse_line(line, p, kept, section))
            return -1;
        if (line->kind == LINE_SECTION)
            section = line->section;
        else if (line->kind == LINE_BAD)
        {
            fprintf(stderr, "conf: %s:%d: not a setting, kept as it is: %s\n", origin, number,
                    line->text);
            bad++;
        }
        p += length;
        if (*p == '\n')
            p++;
    }
    return bad;
}

/**
 * the last line with this section and key.
 * @return its index, or -1.
 */
static int doc_find(const doc_t *doc, const char *section, const char *key)
{
    for (int i = doc->count - 1; i >= 0; i--)
    {
        const line_t *line = &doc->lines[i];
        if (line->kind == LINE_VALUE && strcmp(line->section, section) == 0 &&
            strcmp(line->key, key) == 0)
            return i;
    }
    return -1;
}

static bool is_blank(const line_t *line)
{
    const char *t = line->text;
    while (isspace((unsigned char)*t))
        t++;
    return *t == '\0';
}

/**
 * where a new key of a section goes: after the last key of the section's
 * last part (a comment after it stays below), else just after the header.
 * the part before the first header has no header: a new key there goes
 * after its comments, before the blank lines.
 * @return the index, or -1 if the section is not there.
 */
static int section_end(const doc_t *doc, const char *section)
{
    int start = -1; // the line after the header of the last part
    if (section[0] == '\0')
        start = 0;
    for (int i = 0; i < doc->count; i++)
    {
        if (doc->lines[i].kind == LINE_SECTION && strcmp(doc->lines[i].section, section) == 0)
            start = i + 1;
    }
    if (start < 0)
        return -1;
    int last_key = -1;
    int last_text = start;
    for (int i = start; i < doc->count && doc->lines[i].kind != LINE_SECTION; i++)
    {
        if (doc->lines[i].kind == LINE_VALUE || doc->lines[i].kind == LINE_BAD)
            last_key = i + 1;
        if (!is_blank(&doc->lines[i]))
            last_text = i + 1;
    }
    if (last_key >= 0)
        return last_key;
    return section[0] == '\0' ? last_text : start;
}

/**
 * read a whole file.
 * @return the text to free, or NULL (no file or no memory).
 */
static char *read_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    size_t size = 0;
    size_t capacity = 4096;
    char *text = malloc(capacity);
    while (text)
    {
        if (size + 1 >= capacity)
        {
            capacity *= 2;
            char *bigger = realloc(text, capacity);
            if (!bigger)
            {
                free(text);
                text = NULL;
                break;
            }
            text = bigger;
        }
        ssize_t n = read(fd, text + size, capacity - size - 1);
        if (n < 0)
        {
            free(text);
            text = NULL;
            break;
        }
        if (n == 0)
            break;
        size += (size_t)n;
    }
    close(fd);
    if (text)
        text[size] = '\0';
    return text;
}

static conf_t *conf_new(void)
{
    return calloc(1, sizeof(conf_t));
}

/**
 * read text into one layer.
 * @return false if there is no memory.
 */
static bool load_layer(conf_t *conf, doc_t *doc, const char *text, const char *origin)
{
    if (!text)
        return true;
    int bad = doc_parse(doc, text, origin);
    if (bad < 0)
        return false;
    conf->bad_lines += bad;
    return true;
}

conf_t *conf_from_text(const char *system_text, const char *user_text)
{
    conf_t *conf = conf_new();
    if (!conf)
        return NULL;
    if (!load_layer(conf, &conf->system, system_text, "defaults") ||
        !load_layer(conf, &conf->user, user_text, "user"))
    {
        conf_free(conf);
        return NULL;
    }
    return conf;
}

conf_t *conf_load(const char *system_path, const char *user_path)
{
    conf_t *conf = conf_new();
    if (!conf)
        return NULL;
    if (user_path)
    {
        conf->user_path = copy_string(user_path);
        if (!conf->user_path)
        {
            conf_free(conf);
            return NULL;
        }
    }
    char *system_text = system_path ? read_file(system_path) : NULL;
    char *user_text = user_path ? read_file(user_path) : NULL;
    bool ok = load_layer(conf, &conf->system, system_text, system_path) &&
              load_layer(conf, &conf->user, user_text, user_path);
    free(system_text);
    free(user_text);
    if (!ok)
    {
        conf_free(conf);
        return NULL;
    }
    return conf;
}

/**
 * the user's home directory: $HOME, else the password file, else "/".
 */
static const char *home_dir(void)
{
    const char *home = getenv("HOME");
    if (home && home[0] == '/')
        return home;
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir && pw->pw_dir[0] == '/')
        return pw->pw_dir;
    return "/";
}

static bool good_name(const char *name)
{
    size_t length = strlen(name);
    if (length == 0 || length > NAME_MAX_LEN)
        return false;
    for (const char *p = name; *p; p++)
    {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_')
            return false;
    }
    return true;
}

conf_t *conf_open(const char *name)
{
    if (!name || !good_name(name))
        return NULL;
    char system_path[PATH_LEN];
    char user_path[PATH_LEN];
    const char *home = home_dir();
    snprintf(system_path, sizeof(system_path), "%s/%s.conf", CONF_SYSTEM_DIR, name);
    int n = snprintf(user_path, sizeof(user_path), "%s%s%s/%s.conf", home,
                     home[strlen(home) - 1] == '/' ? "" : "/", CONF_USER_DIR, name);
    if (n < 0 || (size_t)n >= sizeof(user_path))
        return NULL;
    return conf_load(system_path, user_path);
}

void conf_free(conf_t *conf)
{
    if (!conf)
        return;
    doc_free(&conf->system);
    doc_free(&conf->user);
    free(conf->user_path);
    free(conf);
}

const char *conf_get(const conf_t *conf, const char *section, const char *key,
                     const char *fallback)
{
    if (!conf || !section || !key)
        return fallback;
    int i = doc_find(&conf->user, section, key);
    if (i >= 0)
        return conf->user.lines[i].value;
    i = doc_find(&conf->system, section, key);
    if (i >= 0)
        return conf->system.lines[i].value;
    return fallback;
}

long conf_get_int(const conf_t *conf, const char *section, const char *key, long fallback)
{
    const char *text = conf_get(conf, section, key, NULL);
    if (!text || !*text)
        return fallback;
    const char *digits = text;
    if (*digits == '-' || *digits == '+')
        digits++;
    int base = digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X') ? 16 : 10;
    char *end;
    errno = 0;
    long value = strtol(text, &end, base);
    if (*end || errno || end == text)
        return fallback;
    return value;
}

bool conf_get_bool(const conf_t *conf, const char *section, const char *key, bool fallback)
{
    const char *text = conf_get(conf, section, key, NULL);
    if (!text)
        return fallback;
    static const char *const yes[] = {"true", "yes", "on", "1"};
    static const char *const no[] = {"false", "no", "off", "0"};
    for (size_t i = 0; i < sizeof(yes) / sizeof(yes[0]); i++)
    {
        if (strcasecmp(text, yes[i]) == 0)
            return true;
        if (strcasecmp(text, no[i]) == 0)
            return false;
    }
    return fallback;
}

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

uint32_t conf_get_color(const conf_t *conf, const char *section, const char *key,
                        uint32_t fallback)
{
    const char *text = conf_get(conf, section, key, NULL);
    if (!text || text[0] != '#')
        return fallback;
    size_t length = strlen(text + 1);
    if (length != 6 && length != 8)
        return fallback;
    uint32_t value = 0;
    for (size_t i = 1; i <= length; i++)
    {
        int d = hex_digit(text[i]);
        if (d < 0)
            return fallback;
        value = value << 4 | (uint32_t)d;
    }
    if (length == 6)
        return 0xFF000000u | value;
    return value >> 8 | value << 24; // rrggbbaa to aarrggbb
}

static bool has_line_break(const char *s)
{
    return strchr(s, '\n') || strchr(s, '\r');
}

static bool good_section(const char *section)
{
    return !has_line_break(section) && !strchr(section, '[') && !strchr(section, ']');
}

static bool good_key(const char *key)
{
    size_t length = strlen(key);
    return length > 0 && !has_line_break(key) && !strchr(key, '=') && key[0] != '#' &&
           key[0] != ';' && key[0] != '[' && !isspace((unsigned char)key[0]) &&
           !isspace((unsigned char)key[length - 1]);
}

/**
 * the text of a "key = value" line. a value with spaces at an end, or in
 * double quotes, is put in double quotes so it reads back the same.
 */
static char *value_line(const char *key, const char *value)
{
    size_t length = strlen(value);
    bool quote = length > 0 && (isspace((unsigned char)value[0]) ||
                                isspace((unsigned char)value[length - 1]) ||
                                (length >= 2 && value[0] == '"' && value[length - 1] == '"'));
    size_t size = strlen(key) + length + 8;
    char *text = malloc(size);
    if (text)
        snprintf(text, size, quote ? "%s = \"%s\"" : "%s = %s", key, value);
    return text;
}

int conf_set(conf_t *conf, const char *section, const char *key, const char *value)
{
    if (!conf || !section || !key || !value || !good_section(section) || !good_key(key) ||
        has_line_break(value))
        return -1;

    char *text = value_line(key, value);
    if (!text)
        return -1;
    line_t line;
    bool ok = parse_line(&line, text, strlen(text), section);
    free(text);
    if (!ok)
    {
        line_free(&line);
        return -1;
    }

    doc_t *doc = &conf->user;
    int at = doc_find(doc, section, key);
    if (at >= 0)
    {
        // change the line where it is
        line_free(&doc->lines[at]);
        doc->lines[at] = line;
        return 0;
    }

    at = section_end(doc, section);
    if (at < 0)
    {
        // a new section at the end, after a blank line
        int count = doc->count;
        bool blank = count > 0 && is_blank(&doc->lines[count - 1]);
        size_t size = strlen(section) + 3;
        char *header = malloc(size);
        if (!header)
        {
            line_free(&line);
            return -1;
        }
        snprintf(header, size, "[%s]", section);
        line_t *slot;
        if (count > 0 && !blank)
        {
            slot = doc_insert(doc, doc->count);
            if (!slot || !parse_line(slot, "", 0, ""))
            {
                free(header);
                line_free(&line);
                return -1;
            }
        }
        slot = doc_insert(doc, doc->count);
        ok = slot && parse_line(slot, header, strlen(header), "");
        free(header);
        if (!ok)
        {
            line_free(&line);
            return -1;
        }
        at = doc->count;
    }

    line_t *slot = doc_insert(doc, at);
    if (!slot)
    {
        line_free(&line);
        return -1;
    }
    *slot = line;
    return 0;
}

int conf_set_int(conf_t *conf, const char *section, const char *key, long value)
{
    char text[24];
    snprintf(text, sizeof(text), "%ld", value);
    return conf_set(conf, section, key, text);
}

int conf_set_bool(conf_t *conf, const char *section, const char *key, bool value)
{
    return conf_set(conf, section, key, value ? "true" : "false");
}

int conf_set_color(conf_t *conf, const char *section, const char *key, uint32_t argb)
{
    char text[12];
    uint32_t alpha = argb >> 24;
    if (alpha == 0xFF)
        snprintf(text, sizeof(text), "#%06x", (unsigned)(argb & 0xFFFFFF));
    else
        snprintf(text, sizeof(text), "#%06x%02x", (unsigned)(argb & 0xFFFFFF), (unsigned)alpha);
    return conf_set(conf, section, key, text);
}

int conf_unset(conf_t *conf, const char *section, const char *key)
{
    if (!conf || !section || !key)
        return -1;
    int at;
    while ((at = doc_find(&conf->user, section, key)) >= 0)
        doc_remove(&conf->user, at);
    return 0;
}

char *conf_user_text(const conf_t *conf)
{
    size_t size = 1;
    for (int i = 0; i < conf->user.count; i++)
        size += strlen(conf->user.lines[i].text) + 1;
    char *text = malloc(size);
    if (!text)
        return NULL;
    char *p = text;
    for (int i = 0; i < conf->user.count; i++)
    {
        size_t length = strlen(conf->user.lines[i].text);
        memcpy(p, conf->user.lines[i].text, length);
        p += length;
        *p++ = '\n';
    }
    *p = '\0';
    return text;
}

/**
 * make the directories above path (like mkdir -p of its directory).
 */
static int make_parents(const char *path)
{
    char dir[PATH_LEN];
    if (strlen(path) >= sizeof(dir))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(dir, path);
    for (char *p = dir + 1; *p; p++)
    {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(dir, 0755) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    return 0;
}

static int write_all(int fd, const char *data, size_t size)
{
    while (size > 0)
    {
        ssize_t n = write(fd, data, size);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        data += n;
        size -= (size_t)n;
    }
    return 0;
}

int conf_save(const conf_t *conf)
{
    if (!conf || !conf->user_path)
    {
        errno = EINVAL;
        return -1;
    }
    char temp[PATH_LEN];
    if ((size_t)snprintf(temp, sizeof(temp), "%s.new", conf->user_path) >= sizeof(temp))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (make_parents(conf->user_path) != 0)
        return -1;

    char *text = conf_user_text(conf);
    if (!text)
    {
        errno = ENOMEM;
        return -1;
    }
    int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        free(text);
        return -1;
    }
    int result = write_all(fd, text, strlen(text));
    free(text);
    if (result == 0)
        result = fsync(fd);
    int saved = errno;
    close(fd);
    if (result == 0)
    {
        result = rename(temp, conf->user_path);
        saved = errno;
    }
    if (result != 0)
    {
        unlink(temp);
        errno = saved;
    }
    return result;
}

int conf_bad_lines(const conf_t *conf)
{
    return conf ? conf->bad_lines : 0;
}

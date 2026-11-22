/**
 * @file apps.c
 * @brief libapps: read the .app files of the desktop's programs.
 */

#include "apps.h"
#include "conf.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SUFFIX     ".app"
#define SECTION    "app"
#define PATH_LEN   256
#define ID_MAX_LEN 64

static const char *const categories[] = {APP_CATEGORIES};
#define CATEGORY_COUNT ((int)(sizeof(categories) / sizeof(categories[0])))

static void app_clear(app_t *app)
{
    free(app->id);
    free(app->name);
    free(app->comment);
    free(app->exec);
    free(app->icon);
    free(app->category);
    memset(app, 0, sizeof(*app));
}

void apps_free(app_list_t *list)
{
    if (!list)
        return;
    for (int i = 0; i < list->count; i++)
        app_clear(&list->apps[i]);
    free(list->apps);
    free(list);
}

static char *copy_string(const char *s)
{
    size_t length = strlen(s);
    char *copy = malloc(length + 1);
    if (copy)
        memcpy(copy, s, length + 1);
    return copy;
}

/**
 * the category as it is in APP_CATEGORIES (any letter case in the file),
 * else "Other".
 */
static const char *known_category(const char *name)
{
    for (int i = 0; i < CATEGORY_COUNT; i++)
    {
        if (strcasecmp(name, categories[i]) == 0)
            return categories[i];
    }
    return categories[CATEGORY_COUNT - 1];
}

/**
 * read one .app file.
 * @return 1 if app is filled, 0 if the file is skipped, -1 if there is no
 *         memory.
 */
static int read_app(const char *path, const char *id, app_t *app)
{
    conf_t *conf = conf_load(NULL, path);
    if (!conf)
        return -1;
    const char *name = conf_get(conf, SECTION, "name", "");
    const char *exec = conf_get(conf, SECTION, "exec", "");
    if (!*name || !*exec)
    {
        fprintf(stderr, "apps: %s: no name or exec, skipped\n", path);
        conf_free(conf);
        return 0;
    }
    memset(app, 0, sizeof(*app));
    app->id = copy_string(id);
    app->name = copy_string(name);
    app->comment = copy_string(conf_get(conf, SECTION, "comment", ""));
    app->exec = copy_string(exec);
    app->icon = copy_string(conf_get(conf, SECTION, "icon", ""));
    app->category = copy_string(known_category(conf_get(conf, SECTION, "category", "")));
    app->hidden = conf_get_bool(conf, SECTION, "hidden", false);
    conf_free(conf);
    if (!app->id || !app->name || !app->comment || !app->exec || !app->icon || !app->category)
    {
        app_clear(app);
        return -1;
    }
    return 1;
}

static int by_name(const void *a, const void *b)
{
    const app_t *x = a;
    const app_t *y = b;
    int order = strcasecmp(x->name, y->name);
    return order ? order : strcmp(x->id, y->id);
}

app_list_t *apps_load(const char *dir)
{
    app_list_t *list = calloc(1, sizeof(*list));
    if (!list)
        return NULL;
    DIR *d = opendir(dir);
    if (!d)
        return list; // no programs
    int capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL)
    {
        size_t length = strlen(entry->d_name);
        size_t suffix = strlen(SUFFIX);
        if (length <= suffix || length - suffix > ID_MAX_LEN ||
            strcmp(entry->d_name + length - suffix, SUFFIX) != 0)
            continue;
        char id[ID_MAX_LEN + 1];
        memcpy(id, entry->d_name, length - suffix);
        id[length - suffix] = '\0';
        char path[PATH_LEN];
        if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name) >= sizeof(path))
            continue;

        if (list->count == capacity)
        {
            capacity = capacity ? capacity * 2 : 16;
            app_t *apps = realloc(list->apps, (size_t)capacity * sizeof(app_t));
            if (!apps)
            {
                closedir(d);
                apps_free(list);
                return NULL;
            }
            list->apps = apps;
        }
        int result = read_app(path, id, &list->apps[list->count]);
        if (result < 0)
        {
            closedir(d);
            apps_free(list);
            return NULL;
        }
        list->count += result;
    }
    closedir(d);
    if (list->count > 1)
        qsort(list->apps, (size_t)list->count, sizeof(app_t), by_name);
    return list;
}

const app_t *apps_find(const app_list_t *list, const char *id)
{
    if (!list || !id)
        return NULL;
    for (int i = 0; i < list->count; i++)
    {
        if (strcmp(list->apps[i].id, id) == 0)
            return &list->apps[i];
    }
    return NULL;
}

int app_split_exec(const char *exec, char *buffer, char *argv[APP_ARGS_MAX + 1])
{
    int count = 0;
    const char *in = exec;
    char *out = buffer;
    for (;;)
    {
        while (isspace((unsigned char)*in))
            in++;
        if (!*in)
            break;
        if (count == APP_ARGS_MAX)
            return -1;
        argv[count++] = out;
        bool quoted = false;
        while (*in && (quoted || !isspace((unsigned char)*in)))
        {
            if (*in == '"')
                quoted = !quoted;
            else
                *out++ = *in;
            in++;
        }
        if (quoted)
            return -1;
        *out++ = '\0';
    }
    argv[count] = NULL;
    return count > 0 ? count : -1;
}

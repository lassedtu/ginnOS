/**
 * @file recent.c
 * @brief asgard: the programs started last from the launcher, newest
 *        first, kept in asgard.conf ([launcher] recent = "saga huginn").
 */

#include "asgard.h"

#include <conf.h>
#include <stdio.h>
#include <string.h>

#define RECENT_MAX 8
#define ID_MAX     64

static char recent[RECENT_MAX][ID_MAX];
static int recent_count;

void recent_load(void)
{
    recent_count = 0;
    conf_t *conf = conf_open("asgard");
    if (!conf)
        return;
    char ids[RECENT_MAX * ID_MAX];
    snprintf(ids, sizeof(ids), "%s", conf_get(conf, "launcher", "recent", ""));
    conf_free(conf);
    for (char *id = strtok(ids, " "); id && recent_count < RECENT_MAX; id = strtok(NULL, " "))
        snprintf(recent[recent_count++], ID_MAX, "%s", id);
}

void recent_used(const char *id)
{
    if (!id || !id[0] || strlen(id) >= ID_MAX)
        return;
    // to the front; the same id further back goes
    int at = recent_count < RECENT_MAX ? recent_count : RECENT_MAX - 1;
    for (int i = 0; i < recent_count; i++)
        if (strcmp(recent[i], id) == 0)
            at = i;
    memmove(recent[1], recent[0], (size_t)at * ID_MAX);
    snprintf(recent[0], ID_MAX, "%s", id);
    if (at == recent_count && recent_count < RECENT_MAX)
        recent_count++;

    conf_t *conf = conf_open("asgard");
    if (!conf)
        return;
    char ids[RECENT_MAX * ID_MAX] = "";
    for (int i = 0; i < recent_count; i++)
    {
        if (i)
            strncat(ids, " ", sizeof(ids) - strlen(ids) - 1);
        strncat(ids, recent[i], sizeof(ids) - strlen(ids) - 1);
    }
    conf_set(conf, "launcher", "recent", ids);
    conf_save(conf);
    conf_free(conf);
}

int recent_rank(const char *id)
{
    for (int i = 0; i < recent_count; i++)
        if (strcmp(recent[i], id) == 0)
            return i;
    return -1;
}

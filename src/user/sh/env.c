/**
 * @file env.c
 * @brief Environment variable management for the shell.
 *
 * This file implements functions for managing environment variables in a shell-like environment. It allows setting, getting, and unsetting environment variables, as well as iterating through them.
 */

#include "env.h"

#include <string.h>

typedef struct
{
    char key[ENV_KEY_MAX];
    char value[ENV_VAL_MAX];
    int used;
} env_entry_t;

static env_entry_t env_table[ENV_MAX];
static int env_used_count;

void env_init(void)
{
    env_used_count = 0;
    for (int i = 0; i < ENV_MAX; i++)
        env_table[i].used = 0;
}

/**
 * find an entry by key.
 * @return index in env_table, or -1 if not found.
 */
static int env_find(const char *key)
{
    for (int i = 0; i < ENV_MAX; i++)
    {
        if (env_table[i].used && strcmp(env_table[i].key, key) == 0)
            return i;
    }
    return -1;
}

int env_set(const char *key, const char *value)
{
    if (!key || !value)
        return -1;

    // check if it already exists
    int idx = env_find(key);
    if (idx >= 0)
    {
        strncpy(env_table[idx].value, value, ENV_VAL_MAX - 1);
        env_table[idx].value[ENV_VAL_MAX - 1] = '\0';
        return 0;
    }

    // find a free slot
    for (int i = 0; i < ENV_MAX; i++)
    {
        if (!env_table[i].used)
        {
            strncpy(env_table[i].key, key, ENV_KEY_MAX - 1);
            env_table[i].key[ENV_KEY_MAX - 1] = '\0';
            strncpy(env_table[i].value, value, ENV_VAL_MAX - 1);
            env_table[i].value[ENV_VAL_MAX - 1] = '\0';
            env_table[i].used = 1;
            env_used_count++;
            return 0;
        }
    }

    return -1; // table full
}

const char *env_get(const char *key)
{
    if (!key)
        return NULL;

    int idx = env_find(key);
    if (idx >= 0)
        return env_table[idx].value;

    return NULL;
}

int env_unset(const char *key)
{
    if (!key)
        return -1;

    int idx = env_find(key);
    if (idx >= 0)
    {
        env_table[idx].used = 0;
        env_table[idx].key[0] = '\0';
        env_table[idx].value[0] = '\0';
        env_used_count--;
        return 0;
    }

    return -1;
}

int env_count(void)
{
    return env_used_count;
}

int env_get_by_index(int index, char *key_out, char *val_out)
{
    int count = 0;

    for (int i = 0; i < ENV_MAX; i++)
    {
        if (env_table[i].used)
        {
            if (count == index)
            {
                strcpy(key_out, env_table[i].key);
                strcpy(val_out, env_table[i].value);
                return 0;
            }
            count++;
        }
    }

    return -1;
}

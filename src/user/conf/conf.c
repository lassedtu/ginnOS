/**
 * @file conf.c
 * @brief read and change the settings files from the shell.
 *
 * usage: conf <name> <section>.<key>            print the value
 *        conf <name> <section>.<key> <value>    set it in the user's file
 *        conf -u <name> <section>.<key>         remove the user's value (the
 *                                               default is used again)
 *
 * <name> is the settings file: theme is ~/.config/ginnos/theme.conf over
 * /etc/ginnos/theme.conf. a key without a section is "<key>" alone. after
 * a change, the window server (if it runs) tells all open programs.
 */

#include "conf.h"
#include "heimdall.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf("usage: conf <name> <section>.<key> [value]\n"
           "       conf -u <name> <section>.<key>\n");
}

/**
 * split "section.key" at its first dot. no dot: the section is "".
 * @return false if the text is too long.
 */
static bool split_key(const char *text, char *section, size_t size, const char **key)
{
    const char *dot = strchr(text, '.');
    if (!dot)
    {
        section[0] = '\0';
        *key = text;
        return true;
    }
    size_t length = (size_t)(dot - text);
    if (length >= size)
        return false;
    memcpy(section, text, length);
    section[length] = '\0';
    *key = dot + 1;
    return true;
}

/** tell the window server's clients, if it runs. */
static void tell_server(const char *name)
{
    heim_conn_t *conn = heim_connect();
    if (!conn)
        return; // no window server: nobody to tell
    heim_settings_changed(conn, name);
    heim_disconnect(conn);
}

int main(int argc, char **argv)
{
    bool unset = argc > 1 && strcmp(argv[1], "-u") == 0;
    int first = unset ? 2 : 1;
    int words = argc - first;
    if (words < 2 || words > 3 || (unset && words != 2))
    {
        usage();
        return 1;
    }
    const char *name = argv[first];
    char section[64];
    const char *key;
    if (!split_key(argv[first + 1], section, sizeof(section), &key))
    {
        printf("conf: the section name is too long\n");
        return 1;
    }

    conf_t *conf = conf_open(name);
    if (!conf)
    {
        printf("conf: %s: not a settings name (letters, digits, - and _)\n", name);
        return 1;
    }

    int result = 0;
    if (words == 2 && !unset)
    {
        const char *value = conf_get(conf, section, key, NULL);
        if (value)
            printf("%s\n", value);
        else
            result = 1;
        conf_free(conf);
        return result;
    }

    if (unset)
        result = conf_unset(conf, section, key);
    else
        result = conf_set(conf, section, key, argv[first + 2]);
    if (result != 0)
    {
        printf("conf: not a good section, key or value\n");
        conf_free(conf);
        return 1;
    }
    if (conf_save(conf) != 0)
    {
        printf("conf: cannot write the settings: %s\n", strerror(errno));
        conf_free(conf);
        return 1;
    }
    conf_free(conf);
    tell_server(name);
    return 0;
}

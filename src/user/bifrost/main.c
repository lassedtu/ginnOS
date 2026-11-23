/**
 * @file main.c
 * @brief Bifrost, the login screen: shows the login screen, runs the
 *        session of the user who logs in, and shows the login screen
 *        again when they log out.
 *
 * usage: heimdalld -x bifrost   (as root; init starts it at boot)
 *
 * the user who logged in last is chosen first (bifrost.conf in root's
 * home: [login] last).
 */

#include "bifrost.h"

#include <conf.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void remember(const char *name)
{
    conf_t *conf = conf_open("bifrost");
    if (!conf)
        return;
    conf_set(conf, "login", "last", name);
    conf_save(conf);
    conf_free(conf);
}

static void last_user(char *out, size_t length)
{
    out[0] = '\0';
    conf_t *conf = conf_open("bifrost");
    if (!conf)
        return;
    snprintf(out, length, "%s", conf_get(conf, "login", "last", ""));
    conf_free(conf);
}

int main(void)
{
    if (geteuid() != 0)
    {
        fprintf(stderr, "bifrost: must run as root (it starts sessions as other users)\n");
        return 1;
    }
    setenv("HOME", "/root", 1); // root's settings, whoever started it

    for (;;)
    {
        char last[USER_NAME_MAX];
        last_user(last, sizeof(last));
        char name[USER_NAME_MAX];
        if (!screen_run(last, name, sizeof(name)))
        {
            fprintf(stderr, "bifrost: no window server, or no user who can log in\n");
            return 1;
        }
        remember(name);
        session_run(name);
    }
}

/**
 * @file users.c
 * @brief Bifrost: the users who can log in, from /etc/passwd.
 */

#include "bifrost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int users_read(login_user_t *out, int max)
{
    FILE *f = fopen("/etc/passwd", "r");
    if (!f)
        return 0;
    int count = 0;
    char line[256];
    while (count < max && fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\n")] = '\0';
        // name:x:uid:gid:gecos:home:shell
        char *fields[7] = {0};
        int n = 0;
        for (char *p = line; p && n < 7; n++)
        {
            fields[n] = p;
            p = strchr(p, ':');
            if (p)
                *p++ = '\0';
        }
        if (n < 7)
            continue;
        uid_t uid = (uid_t)strtoul(fields[2], NULL, 10);
        if ((uid != 0 && uid < 1000) || strstr(fields[6], "nologin"))
            continue;
        login_user_t *u = &out[count++];
        snprintf(u->name, sizeof(u->name), "%s", fields[0]);
        snprintf(u->full_name, sizeof(u->full_name), "%s",
                 fields[4][0] && strcmp(fields[4], fields[0]) != 0 ? fields[4] : fields[0]);
        u->uid = uid;
    }
    fclose(f);
    return count;
}

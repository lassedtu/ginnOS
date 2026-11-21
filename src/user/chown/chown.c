/**
 * @file chown.c
 * @brief Change file owners: chown <user>[:<group>] <file>...
 *
 * user and group are names (from /etc/passwd and /etc/group) or numbers.
 */

#include <grp.h>
#include <pwd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int is_number(const char *text)
{
    for (; *text; text++)
        if (*text < '0' || *text > '9')
            return 0;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("usage: chown <user>[:<group>] <file>...\n");
        return 1;
    }

    char spec[64];
    strncpy(spec, argv[1], sizeof(spec) - 1);
    spec[sizeof(spec) - 1] = '\0';
    char *group_name = strchr(spec, ':');
    if (group_name)
        *group_name++ = '\0';

    uid_t uid = (uid_t)-1;
    if (spec[0])
    {
        struct passwd *user = is_number(spec) ? NULL : getpwnam(spec);
        if (!user && !is_number(spec))
        {
            printf("chown: unknown user '%s'\n", spec);
            return 1;
        }
        uid = user ? user->pw_uid : (uid_t)atoi(spec);
    }

    gid_t gid = (gid_t)-1;
    if (group_name && group_name[0])
    {
        struct group *group = is_number(group_name) ? NULL : getgrnam(group_name);
        if (!group && !is_number(group_name))
        {
            printf("chown: unknown group '%s'\n", group_name);
            return 1;
        }
        gid = group ? group->gr_gid : (gid_t)atoi(group_name);
    }

    int status = 0;
    for (int i = 2; i < argc; i++)
    {
        if (chown(argv[i], uid, gid) < 0)
        {
            perror(argv[i]);
            status = 1;
        }
    }
    return status;
}

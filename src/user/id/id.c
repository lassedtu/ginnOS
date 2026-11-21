/**
 * @file id.c
 * @brief Print the user and group ids of the process.
 */

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <unistd.h>

/**
 * print "label=id(name)".
 */
static void show(const char *label, unsigned int id, int is_user)
{
    const char *name = NULL;
    if (is_user)
    {
        struct passwd *user = getpwuid(id);
        name = user ? user->pw_name : NULL;
    }
    else
    {
        struct group *group = getgrgid(id);
        name = group ? group->gr_name : NULL;
    }
    printf(name ? "%s=%u(%s)" : "%s=%u", label, id, name);
}

int main(void)
{
    show("uid", getuid(), 1);
    printf(" ");
    show("gid", getgid(), 0);
    if (geteuid() != getuid())
    {
        printf(" ");
        show("euid", geteuid(), 1);
    }
    if (getegid() != getgid())
    {
        printf(" ");
        show("egid", getegid(), 0);
    }
    printf("\n");
    return 0;
}

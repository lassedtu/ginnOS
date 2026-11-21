/**
 * @file whoami.c
 * @brief Print the name of the effective user.
 */

#include <pwd.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    struct passwd *user = getpwuid(geteuid());
    if (user)
        printf("%s\n", user->pw_name);
    else
        printf("%u\n", geteuid());
    return 0;
}

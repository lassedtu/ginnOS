/**
 * @file startde.c
 * @brief start the desktop: heimdalld with the shell, asgard.
 *
 * usage: startde
 *
 * the session ends when asgard ends (Log out in its launcher), and the
 * text console comes back.
 */

#include <stdio.h>
#include <unistd.h>

int main(void)
{
    char *argv[] = {"heimdalld", "-x", "asgard", NULL};
    execvp(argv[0], argv);
    perror("startde: cannot start heimdalld");
    return 1;
}

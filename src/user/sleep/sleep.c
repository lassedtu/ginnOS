/**
 * @file sleep.c
 * @brief Wait for a number of seconds.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void on_alarm(int sig)
{
    (void)sig;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: sleep <seconds>\n");
        return 1;
    }

    int seconds = atoi(argv[1]);
    if (seconds <= 0)
        return 0;

    // SIGALRM wakes pause; any other signal keeps its default action
    signal(SIGALRM, on_alarm);
    alarm((unsigned int)seconds);
    pause();
    return 0;
}

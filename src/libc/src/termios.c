#include "termios.h"

#include <stdbool.h>
#include "errno.h"
#include "stdio.h"
#include "sys/ioctl.h"
#include "unistd.h"

int tcgetattr(int fd, struct termios *t)
{
    return ioctl(fd, TCGETS, t);
}

int tcsetattr(int fd, int when, const struct termios *t)
{
    if (when < TCSANOW || when > TCSAFLUSH)
    {
        errno = EINVAL;
        return -1;
    }
    return ioctl(fd, TCSETS + (unsigned int)when, (void *)t);
}

void cfmakeraw(struct termios *t)
{
    t->c_iflag &= ~(tcflag_t)(INLCR | IGNCR | ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    t->c_oflag &= ~(tcflag_t)OPOST;
    t->c_lflag &= ~(tcflag_t)(ECHO | ECHOE | ECHOK | ICANON | ISIG | IEXTEN);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

pid_t tcgetpgrp(int fd)
{
    pid_t pgrp;
    if (ioctl(fd, TIOCGPGRP, &pgrp) < 0)
    {
        return -1;
    }
    return pgrp;
}

int tcsetpgrp(int fd, pid_t pgrp)
{
    return ioctl(fd, TIOCSPGRP, &pgrp);
}

int isatty(int fd)
{
    struct termios t;
    return tcgetattr(fd, &t) == 0;
}

int ttyctl(int mode)
{
    struct termios t;
    if (tcgetattr(0, &t) < 0)
    {
        return -1;
    }

    int previous = (t.c_lflag & XKEYEVENTS) ? TTY_RAW : TTY_COOKED;
    if (mode == TTY_RAW)
    {
        // key events, no echo, no line editing, and Ctrl+C is a key
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);
        t.c_lflag |= XKEYEVENTS;
    }
    else if (mode == TTY_COOKED)
    {
        t.c_lflag |= ICANON | ECHO | ISIG;
        t.c_lflag &= ~(tcflag_t)XKEYEVENTS;
    }
    else
    {
        errno = EINVAL;
        return -1;
    }

    if (tcsetattr(0, TCSANOW, &t) < 0)
    {
        return -1;
    }
    return previous;
}

char *getpass(const char *prompt)
{
    static char line[128];
    struct termios old;
    bool tty = tcgetattr(0, &old) == 0;
    if (tty)
    {
        struct termios quiet = old;
        quiet.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(0, TCSANOW, &quiet);
    }
    fputs(prompt, stdout);
    fflush(stdout);
    size_t n = 0;
    bool any = false;
    char c;
    while (read(0, &c, 1) == 1)
    {
        any = true;
        if (c == '\n')
            break;
        if (n + 1 < sizeof(line))
            line[n++] = c;
    }
    line[n] = '\0';
    if (tty)
    {
        tcsetattr(0, TCSANOW, &old);
        fputs("\n", stdout);
    }
    return any ? line : NULL;
}

/**
 * @file pty.c
 * @brief pseudo-terminals: posix_openpt and the rest (stdlib.h, pty.h).
 */

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int posix_openpt(int flags)
{
    return open("/dev/ptmx", flags);
}

int grantpt(int fd)
{
    unsigned int index;
    return ioctl(fd, TIOCGPTN, &index) < 0 ? -1 : 0;
}

int unlockpt(int fd)
{
    int unlock = 0;
    return ioctl(fd, TIOCSPTLCK, &unlock) < 0 ? -1 : 0;
}

int ptsname_r(int fd, char *buf, size_t size)
{
    unsigned int index;
    if (ioctl(fd, TIOCGPTN, &index) < 0)
    {
        return errno;
    }
    if (snprintf(buf, size, "/dev/pts/%u", index) >= (int)size)
    {
        errno = ERANGE;
        return ERANGE;
    }
    return 0;
}

char *ptsname(int fd)
{
    static char name[16];
    return ptsname_r(fd, name, sizeof(name)) == 0 ? name : NULL;
}

int openpty(int *master, int *slave, char *name, const struct termios *settings,
            const struct winsize *size)
{
    char path[16];
    int m = posix_openpt(O_RDWR);
    if (m < 0)
    {
        return -1;
    }
    if (unlockpt(m) < 0 || ptsname_r(m, path, sizeof(path)) != 0)
    {
        close(m);
        return -1;
    }
    int s = open(path, O_RDWR);
    if (s < 0)
    {
        close(m);
        return -1;
    }
    if ((settings && tcsetattr(s, TCSANOW, settings) < 0) ||
        (size && ioctl(s, TIOCSWINSZ, (void *)size) < 0))
    {
        close(s);
        close(m);
        return -1;
    }
    if (name)
    {
        strcpy(name, path);
    }
    *master = m;
    *slave = s;
    return 0;
}

int login_tty(int fd)
{
    setsid();
    if (ioctl(fd, TIOCSCTTY, 0) < 0)
    {
        return -1;
    }
    for (int i = 0; i < 3; i++)
    {
        if (dup2(fd, i) < 0)
        {
            return -1;
        }
    }
    if (fd > 2)
    {
        close(fd);
    }
    return 0;
}

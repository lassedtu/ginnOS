#pragma once

/**
 * @file sys/ioctl.h
 * @brief device control requests.
 *
 * the request numbers are ABI (src/kernel/console/termios.h).
 */

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSETSW    0x5403
#define TCSETSF    0x5404
#define TIOCSCTTY  0x540E
#define TIOCGPGRP  0x540F
#define TIOCSPGRP  0x5410
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414     // set the window size (SIGWINCH to the foreground)
#define TIOCGPTN   0x80045430 // pty master: the number N of /dev/pts/N
#define TIOCSPTLCK 0x40045431 // pty master: lock (1) or unlock (0) the slave

struct winsize
{
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int ioctl(int fd, unsigned int request, void *arg);

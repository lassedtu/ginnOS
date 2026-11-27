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
#define FIONREAD   0x541B     // a socket: the bytes of the next datagram (int)

// network interfaces, on any AF_INET socket, with a struct ifreq
// (<net/if.h>). the S calls need root. SIOCGIFGATEWAY and SIOCSIFGATEWAY
// are ginnOS's own: the gateway of the interface, in ifr_addr.
#define SIOCGIFFLAGS   0x8913
#define SIOCSIFFLAGS   0x8914
#define SIOCGIFADDR    0x8915
#define SIOCSIFADDR    0x8916
#define SIOCGIFBRDADDR 0x8919
#define SIOCGIFNETMASK 0x891B
#define SIOCSIFNETMASK 0x891C
#define SIOCGIFMTU     0x8921
#define SIOCGIFHWADDR  0x8927
#define SIOCGIFGATEWAY 0x89F0
#define SIOCSIFGATEWAY 0x89F1
#define TIOCSPTLCK 0x40045431 // pty master: lock (1) or unlock (0) the slave

struct winsize
{
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int ioctl(int fd, unsigned int request, void *arg);

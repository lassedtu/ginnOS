#pragma once

/**
 * @file termios.h
 * @brief terminal settings.
 *
 * the layout, flags and values are ABI and must match the kernel side in
 * src/kernel/console/termios.h exactly (they are the Linux i386 ones).
 */

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef int pid_t;

#define NCCS 19

struct termios
{
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
};

// c_iflag
#define INLCR 0x0040
#define IGNCR 0x0080
#define ICRNL 0x0100
#define IXON  0x0400 // accepted, has no effect
#define BRKINT 0x0002 // accepted, has no effect
#define INPCK 0x0010 // accepted, has no effect
#define ISTRIP 0x0020 // accepted, has no effect

// c_oflag
#define OPOST 0x0001
#define ONLCR 0x0004

// c_cflag
#define CS8   0x0030
#define CREAD 0x0080

// c_lflag
#define ISIG   0x0001
#define ICANON 0x0002
#define ECHO   0x0008
#define ECHOE  0x0010
#define ECHOK  0x0020
#define TOSTOP 0x0100
#define IEXTEN 0x8000
// ginnOS: reads return key_event_t records instead of bytes (unistd.h
// read_event); ttyctl(TTY_RAW) turns this on
#define XKEYEVENTS 0x40000000

// c_cc indexes
#define VINTR  0
#define VQUIT  1
#define VERASE 2
#define VKILL  3
#define VEOF   4
#define VTIME  5
#define VMIN   6
#define VSUSP  10

// tcsetattr when
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2 // also drop typed input not read yet

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int when, const struct termios *t);

/**
 * settings for byte-by-byte input with no echo, no signals and no output
 * processing.
 */
void cfmakeraw(struct termios *t);

/**
 * the foreground process group of the terminal on fd, and setting it.
 */
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgrp);

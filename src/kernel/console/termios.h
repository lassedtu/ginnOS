#pragma once

/**
 * the console's terminal settings (termios) and the job-control ioctls.
 * the layout, flags and request numbers are ABI: the libc's <termios.h>
 * and <sys/ioctl.h> have the same ones (the Linux i386 values).
 */

#include "common/stdint.h"

#define NCCS 19

struct ktermios
{
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t c_line;
    uint8_t c_cc[NCCS];
};

// c_iflag
#define INLCR 0x0040 // NL to CR on input
#define IGNCR 0x0080 // ignore CR
#define ICRNL 0x0100 // CR to NL on input

// c_oflag
#define OPOST 0x0001 // process output
#define ONLCR 0x0004 // NL to CR NL on output

// c_cflag
#define CS8   0x0030
#define CREAD 0x0080

// c_lflag
#define ISIG    0x0001 // INTR, QUIT and SUSP send signals
#define ICANON  0x0002 // line by line input with editing
#define ECHO    0x0008 // echo input
#define ECHOE   0x0010 // erase echoes as backspace, space, backspace
#define ECHOK   0x0020 // kill echoes a newline
#define TOSTOP  0x0100 // background writes get SIGTTOU (not yet honored)
#define IEXTEN  0x8000
// ginnOS: reads return keyboard_event_t records (keys and special keys)
// instead of bytes; this is what ttyctl(TTY_RAW) turns on
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

// ioctl requests on the console
#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSETSW    0x5403
#define TCSETSF    0x5404 // set, and drop typed input not read yet
#define TIOCSCTTY  0x540E
#define TIOCGPGRP  0x540F
#define TIOCSPGRP  0x5410
#define TIOCGWINSZ 0x5413

struct kwinsize
{
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

/**
 * set up the console terminal: default settings, and init's session as
 * the one it controls (with init's group in the foreground). call once at
 * boot, before the first process runs.
 * @param init_pid the pid init will have (its session and group id).
 */
void console_terminal_init(uint32_t init_pid);

#pragma once

/**
 * @file pty.h
 * @brief pseudo-terminals: /dev/ptmx and /dev/pts/N.
 *
 * a terminal app (mimir) opens /dev/ptmx and gets the master side of a new
 * pair. the program it runs (a shell) uses the slave side, /dev/pts/N, as
 * its terminal: the same termios settings, job control ioctls and window
 * size as the console. bytes the master writes go through the slave's line
 * discipline (line editing, echo, the signal keys) and are read by the
 * slave; bytes the slave writes are read by the master.
 *
 * the user interface is the Linux one: TIOCGPTN gives N, TIOCSPTLCK with 0
 * unlocks the slave (unlockpt), TIOCSWINSZ sets the size and sends SIGWINCH
 * to the foreground process group.
 *
 * limits: VTIME is not used (a read with VMIN 0 does not wait at all). the
 * master's writes wait when the slave does not read; a terminal app that
 * also reads the master in the same thread uses O_NONBLOCK and poll.
 */

#include "common/stdint.h"

struct device;

// the most pairs open at the same time
#define PTY_MAX 16

/**
 * register /dev/ptmx. call once at boot, after the device registry.
 */
void pty_init(void);

/**
 * the device of /dev/pts/N (devfs uses it to open the slave side).
 * @return the device, or NULL if pair N does not exist.
 */
struct device *pty_slave_device(uint32_t index);

/**
 * the lowest pair number at or above *index that exists (devfs lists them).
 * @param index in: where to start; out: the pair found.
 * @return false if there is none.
 */
bool pty_next(uint32_t *index);

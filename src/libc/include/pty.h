#pragma once

#include <sys/ioctl.h>
#include <termios.h>

/**
 * @file pty.h
 * @brief open a pseudo-terminal pair, and make one a process's terminal.
 */

/**
 * open a new pseudo-terminal pair.
 * @param master receives the master fd.
 * @param slave receives the slave fd.
 * @param name receives the slave's path, if not NULL (at least 16 bytes).
 * @param settings the slave's termios, if not NULL.
 * @param size the window size, if not NULL.
 * @return 0, or -1 with errno set.
 */
int openpty(int *master, int *slave, char *name, const struct termios *settings,
            const struct winsize *size);

/**
 * make fd the terminal of the calling process: start a new session, make
 * fd its controlling terminal, put fd on stdin, stdout and stderr, and
 * close fd. for the child of a terminal app, before execve.
 * @return 0, or -1 with errno set.
 */
int login_tty(int fd);

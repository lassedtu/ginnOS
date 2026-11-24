#pragma once

#include <sys/types.h>

/**
 * @file sys/resource.h
 * @brief process priorities: the nice value, -20 (the most CPU) to 19
 *        (the least). ginnOS gives a process with a lower nice value a
 *        longer time slice.
 */

#define PRIO_PROCESS 0 // who is a process id (0: the caller)
#define PRIO_PGRP    1 // who is a process group (0: the caller's)
#define PRIO_USER    2 // who is a user id (0: the caller's)

/**
 * the lowest nice value of the processes which and who name.
 * @return the value, or -1 with errno set (-1 is also a nice value: set
 *         errno to 0 first to tell them apart).
 */
int getpriority(int which, id_t who);

/**
 * set the nice value of processes. only root may lower it.
 * @return 0, or -1 (errno: ESRCH, EPERM, EACCES).
 */
int setpriority(int which, id_t who, int value);

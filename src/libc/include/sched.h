#pragma once

/**
 * @file sched.h
 * @brief the scheduler: give up the CPU. ginnOS has one policy for all
 *        processes (the nice value changes the share of each).
 */

#define SCHED_OTHER 0
#define SCHED_FIFO  1 // real-time policies: refused (EPERM)
#define SCHED_RR    2

struct sched_param
{
    int sched_priority;
};

/**
 * give the CPU to another thread that is ready to run.
 * @return 0.
 */
int sched_yield(void);

/**
 * the range of priorities of a policy: 0 to 0 for SCHED_OTHER, 1 to 99
 * for the real-time policies (as on Linux).
 * @return the priority, or -1 with errno EINVAL for another policy.
 */
int sched_get_priority_min(int policy);
int sched_get_priority_max(int policy);

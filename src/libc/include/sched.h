#pragma once

/**
 * @file sched.h
 * @brief the scheduler: give up the CPU. ginnOS has one policy for all
 *        processes (the nice value changes the share of each).
 */

#define SCHED_OTHER 0

struct sched_param
{
    int sched_priority;
};

/**
 * give the CPU to another thread that is ready to run.
 * @return 0.
 */
int sched_yield(void);

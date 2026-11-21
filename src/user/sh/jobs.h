/**
 * @file jobs.h
 * @brief Job control: background jobs, stopped jobs, fg and bg.
 *
 * every pipeline runs in a process group of its own. a foreground job
 * gets the terminal (tcsetpgrp) until it exits or stops; a background job
 * (cmd &) runs while the shell reads more commands. the shell ignores the
 * job-control signals itself, so Ctrl+C and Ctrl+Z reach only the job.
 */
#pragma once

#include <sys/types.h>
#include <termios.h>

#define JOBS_MAX 16
#define JOB_COMMAND_MAX 128
#define JOB_PIDS_MAX 8

typedef enum
{
    JOB_FREE = 0,
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE, // every process ended; reported once, then freed
} job_state_t;

typedef struct
{
    int id;                       // the number the user sees (%1)
    pid_t pgid;                   // its process group
    pid_t pids[JOB_PIDS_MAX];     // its processes
    int pid_count;
    int alive;                    // processes not reaped yet
    pid_t last_pid;               // the last stage (its status is the job's)
    int last_status;              // wait status of the last stage
    job_state_t state;
    struct termios modes;         // the terminal settings when it stopped
    int has_modes;
    char command[JOB_COMMAND_MAX];
} job_t;

/**
 * set up job control: the shell leads its own group, owns the terminal and
 * ignores SIGINT, SIGQUIT, SIGTSTP, SIGTTIN and SIGTTOU.
 */
void jobs_init(void);

/**
 * in a forked child, before execve: join the job's group (0: lead a new
 * one), take the terminal for a foreground job, and get the default
 * action back for the signals the shell ignores.
 * @param pgid the job's group, or 0 for the first process of a job.
 * @param foreground true for a foreground job.
 */
void jobs_child_setup(pid_t pgid, int foreground);

/**
 * record a job the shell has just started.
 * @return the job, or NULL if the table is full.
 */
job_t *jobs_add(pid_t pgid, const pid_t *pids, int count, const char *command,
                int foreground);

/**
 * give a job the terminal and wait until it ends or stops; then take the
 * terminal back. sets shell_last_exit_code.
 * @param job the job.
 */
void jobs_wait_foreground(job_t *job);

/**
 * collect children that ended, stopped or continued without waiting, and
 * print a line for each background job that finished or stopped. called
 * before each prompt.
 */
void jobs_report(void);

/**
 * the builtins jobs, fg, bg and kill (which also takes %job).
 * @return 1 (handled).
 */
int jobs_builtin_jobs(int argc, char **argv);
int jobs_builtin_fg(int argc, char **argv);
int jobs_builtin_bg(int argc, char **argv);
int jobs_builtin_kill(int argc, char **argv);

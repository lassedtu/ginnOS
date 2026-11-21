/**
 * @file jobs.c
 * @brief Job control for the shell (see jobs.h).
 */

#include "jobs.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern int shell_last_exit_code;

static job_t jobs[JOBS_MAX];
static pid_t shell_pgid;
static struct termios shell_modes;

// the signals the shell ignores and its children must not
static const int job_signals[] = {SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU};
#define JOB_SIGNAL_COUNT 5

void jobs_init(void)
{
    for (int i = 0; i < JOB_SIGNAL_COUNT; i++)
        signal(job_signals[i], SIG_IGN);

    // the shell leads its own group and has the terminal
    shell_pgid = getpid();
    setpgid(0, shell_pgid);
    tcsetpgrp(0, shell_pgid);
    tcgetattr(0, &shell_modes);
}

void jobs_child_setup(pid_t pgid, int foreground)
{
    pid_t self = getpid();
    if (pgid == 0)
        pgid = self;

    // both the shell and the child set the group, so neither depends on
    // which of them runs first
    setpgid(0, pgid);
    if (foreground)
        tcsetpgrp(0, pgid);

    for (int i = 0; i < JOB_SIGNAL_COUNT; i++)
        signal(job_signals[i], SIG_DFL);
}

/**
 * the lowest job number not in use.
 */
static int next_job_id(void)
{
    for (int id = 1;; id++)
    {
        int used = 0;
        for (int i = 0; i < JOBS_MAX; i++)
            used |= jobs[i].state != JOB_FREE && jobs[i].id == id;
        if (!used)
            return id;
    }
}

job_t *jobs_add(pid_t pgid, const pid_t *pids, int count, const char *command,
                int foreground)
{
    for (int i = 0; i < JOBS_MAX; i++)
    {
        job_t *job = &jobs[i];
        if (job->state != JOB_FREE)
            continue;

        memset(job, 0, sizeof(*job));
        job->id = next_job_id();
        job->pgid = pgid;
        job->pid_count = count < JOB_PIDS_MAX ? count : JOB_PIDS_MAX;
        for (int p = 0; p < job->pid_count; p++)
            job->pids[p] = pids[p];
        job->alive = job->pid_count;
        job->last_pid = pids[count - 1];
        job->state = JOB_RUNNING;
        strncpy(job->command, command, JOB_COMMAND_MAX - 1);

        if (!foreground)
            printf("[%d] %d\n", job->id, pgid);
        return job;
    }
    return NULL;
}

/**
 * the job a process belongs to.
 */
static job_t *job_of(pid_t pid)
{
    for (int i = 0; i < JOBS_MAX; i++)
    {
        job_t *job = &jobs[i];
        if (job->state == JOB_FREE)
            continue;
        for (int p = 0; p < job->pid_count; p++)
            if (job->pids[p] == pid)
                return job;
    }
    return NULL;
}

/**
 * apply one waitpid result to its job.
 */
static void update_job(pid_t pid, int status)
{
    job_t *job = job_of(pid);
    if (!job)
        return;

    if (WIFSTOPPED(status))
    {
        job->state = JOB_STOPPED;
    }
    else if (WIFCONTINUED(status))
    {
        job->state = JOB_RUNNING;
    }
    else
    {
        job->alive--;
        if (pid == job->last_pid)
            job->last_status = status;
        if (job->alive == 0)
            job->state = JOB_DONE;
    }
}

/**
 * the exit code a job's status gives $?.
 */
static int exit_code_of(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSTOPPED(status))
        return 128 + WSTOPSIG(status);
    return 128 + WTERMSIG(status);
}

static void free_job(job_t *job)
{
    job->state = JOB_FREE;
}

void jobs_wait_foreground(job_t *job)
{
    tcsetpgrp(0, job->pgid);

    while (job->state == JOB_RUNNING)
    {
        int status;
        pid_t pid = waitpid(-1, &status, WUNTRACED);
        if (pid < 0)
            break; // no children left
        update_job(pid, status);
    }

    // the shell takes the terminal back, with its own settings; a stopped
    // job's settings are kept for fg
    tcsetpgrp(0, shell_pgid);
    if (job->state == JOB_STOPPED)
    {
        job->has_modes = tcgetattr(0, &job->modes) == 0;
        printf("\n[%d] stopped  %s\n", job->id, job->command);
        shell_last_exit_code = 128 + SIGTSTP;
    }
    else
    {
        shell_last_exit_code = exit_code_of(job->last_status);
        free_job(job);
    }
    tcsetattr(0, TCSANOW, &shell_modes);
}

void jobs_report(void)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
        update_job(pid, status);

    for (int i = 0; i < JOBS_MAX; i++)
    {
        job_t *job = &jobs[i];
        if (job->state == JOB_DONE)
        {
            printf("[%d] done  %s\n", job->id, job->command);
            free_job(job);
        }
    }
}

/**
 * the job an argument names: %N or N, or with no argument the newest one.
 */
static job_t *find_job(int argc, char **argv)
{
    if (argc < 2)
    {
        job_t *newest = NULL;
        for (int i = 0; i < JOBS_MAX; i++)
            if (jobs[i].state != JOB_FREE && (!newest || jobs[i].id > newest->id))
                newest = &jobs[i];
        return newest;
    }

    const char *arg = argv[1][0] == '%' ? argv[1] + 1 : argv[1];
    int id = atoi(arg);
    for (int i = 0; i < JOBS_MAX; i++)
        if (jobs[i].state != JOB_FREE && jobs[i].id == id)
            return &jobs[i];
    return NULL;
}

int jobs_builtin_jobs(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    jobs_report();
    for (int i = 0; i < JOBS_MAX; i++)
    {
        job_t *job = &jobs[i];
        if (job->state == JOB_RUNNING || job->state == JOB_STOPPED)
            printf("[%d] %s  %s\n", job->id, job->state == JOB_RUNNING ? "running" : "stopped",
                   job->command);
    }
    return 1;
}

int jobs_builtin_fg(int argc, char **argv)
{
    job_t *job = find_job(argc, argv);
    if (!job)
    {
        printf("fg: no such job\n");
        shell_last_exit_code = 1;
        return 1;
    }

    printf("%s\n", job->command);
    if (job->has_modes)
        tcsetattr(0, TCSANOW, &job->modes);
    tcsetpgrp(0, job->pgid);
    job->state = JOB_RUNNING;
    kill(-job->pgid, SIGCONT);
    jobs_wait_foreground(job);
    return 1;
}

int jobs_builtin_bg(int argc, char **argv)
{
    job_t *job = find_job(argc, argv);
    if (!job || job->state != JOB_STOPPED)
    {
        printf("bg: no stopped job\n");
        shell_last_exit_code = 1;
        return 1;
    }

    job->state = JOB_RUNNING;
    printf("[%d] %s &\n", job->id, job->command);
    kill(-job->pgid, SIGCONT);
    return 1;
}

/**
 * a signal name or number after "-" (kill -9, kill -TERM, kill -STOP).
 */
static int parse_signal(const char *text)
{
    static const struct
    {
        const char *name;
        int number;
    } names[] = {
        {"HUP", SIGHUP}, {"INT", SIGINT}, {"QUIT", SIGQUIT}, {"KILL", SIGKILL},
        {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"PIPE", SIGPIPE}, {"ALRM", SIGALRM},
        {"TERM", SIGTERM}, {"CHLD", SIGCHLD}, {"CONT", SIGCONT}, {"STOP", SIGSTOP},
        {"TSTP", SIGTSTP},
    };

    if (text[0] >= '0' && text[0] <= '9')
        return atoi(text);
    if (strncmp(text, "SIG", 3) == 0)
        text += 3;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcmp(text, names[i].name) == 0)
            return names[i].number;
    return -1;
}

int jobs_builtin_kill(int argc, char **argv)
{
    int sig = SIGTERM;
    int first = 1;
    if (argc > 1 && argv[1][0] == '-')
    {
        sig = parse_signal(argv[1] + 1);
        first = 2;
    }
    if (sig < 0 || first >= argc)
    {
        printf("kill: usage: kill [-signal] pid|%%job ...\n");
        shell_last_exit_code = 1;
        return 1;
    }

    shell_last_exit_code = 0;
    for (int i = first; i < argc; i++)
    {
        pid_t target;
        if (argv[i][0] == '%')
        {
            char *job_argv[2] = {argv[0], argv[i]};
            job_t *job = find_job(2, job_argv);
            if (!job)
            {
                printf("kill: %s: no such job\n", argv[i]);
                shell_last_exit_code = 1;
                continue;
            }
            target = -job->pgid; // the whole job
        }
        else
        {
            target = atoi(argv[i]);
        }

        if (kill(target, sig) < 0)
        {
            printf("kill: %s: no such process\n", argv[i]);
            shell_last_exit_code = 1;
        }
    }
    return 1;
}

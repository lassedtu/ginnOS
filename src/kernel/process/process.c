#include "process.h"
#include "../../common/memory.h"

// the process table — fixed array of PCBs.
static process_t proc_table[PROCESS_MAX];

// next PID to assign (monotonically increasing).
static uint32_t next_pid = 1;

// pointer to the currently running process.
static process_t *current_process = (void *)0;

void process_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    next_pid = 1;
    current_process = (void *)0;
}

process_t *process_create(void)
{
    // find a free slot
    for (int i = 0; i < PROCESS_MAX; i++)
    {
        if (proc_table[i].state == PROC_STATE_UNUSED)
        {
            process_t *proc = &proc_table[i];

            memset(proc, 0, sizeof(process_t));
            proc->pid = next_pid++;
            proc->state = PROC_STATE_READY;
            proc->brk = 0;
            proc->exit_code = 0;

            // initialize fd table: stdin/stdout/stderr as console
            proc->fds[0].type = FD_TYPE_CONSOLE;
            proc->fds[1].type = FD_TYPE_CONSOLE;
            proc->fds[2].type = FD_TYPE_CONSOLE;

            return proc;
        }
    }

    return (void *)0; /* table full */
}

process_t *process_current(void)
{
    return current_process;
}

void process_set_current(process_t *proc)
{
    current_process = proc;
}

void process_destroy(process_t *proc)
{
    if (!proc)
    {
        return;
    }

    // close any open file descriptors
    for (int i = 3; i < FD_MAX; i++)
    {
        if (proc->fds[i].type == FD_TYPE_FILE)
        {
            vfs_close(&proc->fds[i].file);
        }
    }

    proc->state = PROC_STATE_UNUSED;
    proc->pid = PID_NONE;
}

process_t *process_get(uint32_t pid)
{
    if (pid == PID_NONE)
    {
        return (void *)0;
    }

    for (int i = 0; i < PROCESS_MAX; i++)
    {
        if (proc_table[i].state != PROC_STATE_UNUSED &&
            proc_table[i].pid == pid)
        {
            return &proc_table[i];
        }
    }

    return (void *)0;
}

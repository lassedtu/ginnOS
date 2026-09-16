#include "process.h"
#include "kernel/memory/pmm.h"
#include "arch/arch.h"
#include "common/memory.h"

// the process table fixed array of PCBs.
static process_t proc_table[PROCESS_MAX];

// next PID to assign (monotonically increasing).
static uint32_t next_pid = 1;

// pointer to the currently running process.
static process_t *current_process = NULL;

void process_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    next_pid = 1;
    current_process = NULL;
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
            proc->parent_pid = PID_NONE;
            proc->state = PROC_STATE_READY;
            proc->brk = 0;
            proc->exit_code = 0;
            proc->wait_for_pid = PID_NONE;

            // allocate a kernel stack page
            void *stack_page = pmm_alloc_page();
            if (!stack_page)
            {
                return NULL;
            }
            memset(stack_page, 0, KERNEL_STACK_SIZE);
            proc->kernel_stack = (uint32_t)stack_page;
            // ESP starts at the top of the stack (grows downward)
            proc->kernel_esp = (uint32_t)stack_page + KERNEL_STACK_SIZE;

            // allocate a per-process address space (shares kernel mappings)
            uint32_t pd = arch_create_address_space();
            if (pd == ADDR_SPACE_NONE)
            {
                pmm_free_page(stack_page);
                return NULL;
            }
            proc->page_directory = pd;

            // initialize fd table: stdin/stdout/stderr as console
            proc->fds[0].type = FD_TYPE_CONSOLE;
            proc->fds[1].type = FD_TYPE_CONSOLE;
            proc->fds[2].type = FD_TYPE_CONSOLE;

            // default working directory
            proc->cwd[0] = '/';
            proc->cwd[1] = '\0';

            return proc;
        }
    }

    return NULL; /* table full */
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

    // close all open file descriptors (including pipes and fds 0-2).
    // note: this may run for a process that isn't the current one (a parent
    // reaping a zombie child), so we can't use fd_free() here — it operates
    // on process_current(). the pipe close logic is mirrored inline, plus a
    // wake of the opposite end so no peer blocks forever on a dead process.
    for (int i = 0; i < FD_MAX; i++)
    {
        if (proc->fds[i].type == FD_TYPE_FILE)
        {
            vfs_close(&proc->fds[i].file);
            proc->fds[i].type = FD_TYPE_NONE;
        }
        else if (proc->fds[i].type == FD_TYPE_PIPE)
        {
            pipe_buf_t *buf = proc->fds[i].pipe.buf;
            pipe_dir_t dir = proc->fds[i].pipe.dir;

            if (dir == PIPE_READ)
            {
                buf->read_refs--;
                if (buf->read_refs <= 0)
                    wait_queue_wake_all(&buf->writers);
            }
            else
            {
                buf->write_refs--;
                if (buf->write_refs <= 0)
                    wait_queue_wake_all(&buf->readers);
            }

            buf->ref_count--;
            if (buf->ref_count <= 0)
                pipe_release(buf);

            proc->fds[i].type = FD_TYPE_NONE;
        }
    }

    // free the kernel stack
    if (proc->kernel_stack)
    {
        pmm_free_page((void *)proc->kernel_stack);
        proc->kernel_stack = 0;
    }

    // free the per-process address space (and user page tables/frames)
    if (proc->page_directory)
    {
        arch_destroy_address_space(proc->page_directory);
        proc->page_directory = 0;
    }

    proc->state = PROC_STATE_UNUSED;
    proc->pid = PID_NONE;
}

process_t *process_get(uint32_t pid)
{
    if (pid == PID_NONE)
    {
        return NULL;
    }

    for (int i = 0; i < PROCESS_MAX; i++)
    {
        if (proc_table[i].state != PROC_STATE_UNUSED &&
            proc_table[i].pid == pid)
        {
            return &proc_table[i];
        }
    }

    return NULL;
}

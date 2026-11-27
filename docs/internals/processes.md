# Processes, threads and the scheduler

The kernel runs programs as processes. A thread is a process that
shares its memory, its open files and its signal handlers with the other
threads of its group. ginnOS uses one CPU.

The code: [`src/kernel/process/`](https://github.com/lassedtu/ginnOS/tree/main/src/kernel/process),
[`src/kernel/scheduler/`](https://github.com/lassedtu/ginnOS/tree/main/src/kernel/scheduler),
[`src/kernel/sync/`](https://github.com/lassedtu/ginnOS/tree/main/src/kernel/sync),
[`src/kernel/usermode/exec.c`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/usermode/exec.c)

## The process table

The kernel has a fixed table of 64 processes
([`process.h`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/process/process.h)).
Each thread uses one entry. An entry has the state, the IDs, the
registers, a kernel stack, the user space, the open files, the signals
and the credentials.

| State | Meaning |
| --- | --- |
| `NEW` | made, but not ready to run yet |
| `READY` | in the ready queue |
| `RUNNING` | on the CPU |
| `BLOCKED` | waits for an event: data, a child, a timer |
| `STOPPED` | stopped by a signal, until `SIGCONT` |
| `ZOMBIE` | ended. The parent has not read its exit status yet. |

The `pid` of an entry is the ID of the thread. The `tgid` is the `pid`
of the first thread of the group, and `getpid` returns it.

## Make a process

| System call | What it does |
| --- | --- |
| `fork` | copies the process. The memory is shared with copy on write (see [Memory](memory.md#fork-and-copy-on-write)). |
| `clone` | makes a thread: it shares the user space, the files and the signal handlers. `CLONE_SETTLS` sets its thread pointer, and `CLONE_CHILD_CLEARTID` makes the kernel set a word to 0 and wake a futex when the thread ends. libc's `pthread_create` uses it. |
| `execve` | replaces the program of the process with a static ELF file. It honours the set-user-ID and set-group-ID bits. The other threads of the group end. |
| `exit`, `exit_group` | end the thread, or all threads of the group |
| `waitpid` | waits for a child to end, stop or continue, and reads its status |

When a process ends, init (PID 1) becomes the parent of its children.
init collects each child that ends, so no zombie stays.

## The scheduler

The scheduler is round robin: one ready queue, first in, first out. The
timer interrupts 100 times a second. A process runs for its time slice,
then goes to the end of the queue:

| `nice` | Time slice |
| --- | --- |
| -20 | 4 ticks (40 ms) |
| 0 | 2 ticks (20 ms) |
| 1 to 19 | 1 tick (10 ms) |

A process that blocks gives the CPU away at once. When the queue is
empty, the idle process halts the CPU until the next interrupt.

## Sleep and wake

| Tool | Use |
| --- | --- |
| wait queue | A process sleeps until another process or an interrupt wakes it. An interruptible sleep also ends on a signal, and the system call then returns `EINTR`. |
| spinlock | A short section that an interrupt handler also uses. It masks interrupts. |
| mutex | A long section in process context, for example disk and file system work. A process that finds it locked sleeps. |
| completion | A driver waits for its device. It sleeps until the interrupt, or polls if the device has no interrupt. |
| futex | A thread sleeps on a word in user memory. libc builds its mutexes, condition variables and semaphores on it. A futex works only between the threads of one process. |

The rules for locks are in
[`spinlock.h`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/sync/spinlock.h).
Do not sleep in an interrupt handler, and do not sleep while you hold a
spinlock.

## See the processes

`/proc` has a folder for each process, and Huginn and `gfetch` read it.
`/proc/self` is the process that reads it.

# Signals

A signal tells a process about an event: a key such as Ctrl+C, the end
of a child, a write to a closed pipe, or a `kill` from another process.
ginnOS has the POSIX signals, with the Linux numbers.

The code: [`src/kernel/signal/`](https://github.com/lassedtu/ginnOS/tree/main/src/kernel/signal),
[`src/libc/src/signal.c`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/src/signal.c)

## How a signal goes to a process

1. The kernel sets the bit of the signal in the pending mask of the
   process. If the process sleeps interruptibly, it wakes, and its
   system call returns `EINTR`.
2. Before the process goes back to user mode (after each system call
   and each interrupt), the kernel looks at the pending signals that
   the process does not block.
3. For each such signal, the kernel does one of these:
   - nothing, if the process ignores the signal;
   - the default action: end the process, stop it, or nothing;
   - a call to the handler of the process. The kernel puts a frame
     with the registers on the user stack and jumps to the handler. When
     the handler returns, it goes to a restorer function in libc, which
     calls `sigreturn`. `sigreturn` puts the registers back from the
     frame.

If a handler has `SA_RESTART`, a system call that the signal
interrupted runs again after the handler. `signal()` in libc sets
`SA_RESTART`.

## The signals and their defaults

| Default | Signals |
| --- | --- |
| stop the process | `SIGSTOP`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU` |
| do nothing | `SIGCHLD`, `SIGCONT`, `SIGURG`, `SIGWINCH` |
| end the process | all others, for example `SIGINT`, `SIGTERM`, `SIGKILL`, `SIGSEGV`, `SIGPIPE` |

`SIGKILL` and `SIGSTOP` cannot be caught, blocked or ignored. `SIGCONT`
continues a stopped process.

The numbers are in
[`signal.h`](https://github.com/lassedtu/ginnOS/blob/main/src/kernel/signal/signal.h).
They are the same in the libc header, and in the wait status of a
process that a signal ended.

## Who sends signals

| Event | Signal |
| --- | --- |
| Ctrl+C, Ctrl+\, Ctrl+Z on a terminal (with `ISIG`) | `SIGINT`, `SIGQUIT`, `SIGTSTP` to the foreground process group |
| a background process reads from, or writes to, its terminal | `SIGTTIN`, `SIGTTOU` to its process group |
| the size of a pseudo-terminal changes | `SIGWINCH` to the foreground process group |
| the controller side of a pseudo-terminal closes | `SIGHUP` to the session |
| a child ends or stops | `SIGCHLD` to the parent |
| a write to a pipe or a socket with no reader | `SIGPIPE`, and the write returns `EPIPE` (no signal with `MSG_NOSIGNAL`) |
| an access to memory that is not mapped | `SIGSEGV` |
| `alarm` ends | `SIGALRM` |
| `kill` | any signal, to a process or a process group |

## The system calls

| System call | What it does |
| --- | --- |
| `kill` | sends a signal |
| `sigaction` | sets the handler, the mask and the flags (`SA_RESTART`, `SA_NODEFER`, `SA_RESETHAND`) of a signal |
| `sigprocmask` | blocks or unblocks signals |
| `sigpending` | shows the pending signals that are blocked |
| `sigsuspend` | sets a mask and sleeps until a signal comes |
| `sigreturn` | goes back from a handler (only libc calls it) |
| `alarm` | sends `SIGALRM` after a number of seconds |

## Not there yet

- `SA_SIGINFO` and `siginfo_t`
- `sigaltstack`
- real-time signals and queued signals
- `SIGVTALRM` and `SIGPROF` have numbers, but there are no timers that
  send them

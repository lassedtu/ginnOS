<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;signal.h&gt;

Source: [`src/libc/include/signal.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/signal.h)

signals: kill, sigaction, masks.

the numbers, flags and the kernel's sigaction layout are ABI and must
match src/kernel/signal/signal.h exactly.

## sigaction

```c
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
```

read and change what the process does with a signal.

Returns: 0, or -1 (errno set; EINVAL for SIGKILL and SIGSTOP).

## signal

```c
sighandler_t signal(int sig, sighandler_t handler);
```

set a handler (with SA\_RESTART, as BSD signal() does).

Returns: the old handler, or SIG\_ERR.

## kill

```c
int kill(pid_t pid, int sig);
```

send a signal to a process (pid &gt; 0) or every process but init (-1).

Returns: 0, or -1 (errno set).

## raise

```c
int raise(int sig);
```

send a signal to the calling process.

## sigprocmask

```c
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
```

read and change the blocked mask.

## sigpending

```c
int sigpending(sigset_t *set);
```

the signals that are blocked and pending.

## sigsuspend

```c
int sigsuspend(const sigset_t *mask);
```

wait for a signal with a temporary mask. always returns -1 with EINTR.

<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/wait.h&gt;

Source: [`src/libc/include/sys/wait.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/wait.h)

waiting for child processes.

the status word and the option values are ABI and must match the kernel
side in src/kernel/process/wait\_status.h.

## waitpid

```c
pid_t waitpid(pid_t pid, int *status, int options);
```

wait for a child process to exit and reap it.

- `pid`: a child's pid, or -1 for any child.
- `status`: receives the status word (may be NULL).
- `options`: 0 or WNOHANG.

Returns: the reaped child's pid, 0 with WNOHANG if no matching child has exited yet, or -1 on error (errno set; ECHILD if there is no matching child).

## wait

```c
pid_t wait(int *status);
```

wait for any child to exit and reap it.

- `status`: receives the status word (may be NULL).

Returns: the child's pid, or -1 on error (errno set).

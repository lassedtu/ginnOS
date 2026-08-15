/**
 * /bin/waitpid_test test program for SYS_exec + SYS_waitpid.
 * spawns /bin/hello as a child process, waits for it, and reports
 * the exit code.
 */

#define SYS_WRITE 1
#define SYS_EXEC 8
#define SYS_GETPID 9
#define SYS_WAITPID 10

static int write(int fd, const char *buf, int count)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(count));
    return ret;
}

static int exec(const char *path)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_EXEC), "b"(path));
    return ret;
}

static int getpid(void)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GETPID));
    return ret;
}

static int waitpid(int pid)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WAITPID), "b"(pid));
    return ret;
}

static void puts(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    write(1, s, len);
}

static void to_dec(int val, char *buf)
{
    if (val < 0)
    {
        *buf++ = '-';
        val = -val;
    }

    char tmp[12];
    int i = 0;
    if (val == 0)
    {
        tmp[i++] = '0';
    }
    else
    {
        while (val > 0)
        {
            tmp[i++] = '0' + (val % 10);
            val /= 10;
        }
    }

    // reverse
    for (int j = i - 1; j >= 0; j--)
    {
        *buf++ = tmp[j];
    }
    *buf = '\0';
}

int main(void)
{
    char num[12];

    puts("waitpid_test: starting (pid=");
    to_dec(getpid(), num);
    puts(num);
    puts(")\n");

    // spawn /bin/hello as a child
    puts("waitpid_test: spawning /bin/hello...\n");
    int child_pid = exec("/bin/hello");

    if (child_pid < 0)
    {
        puts("waitpid_test: FAIL exec returned error\n");
        return 1;
    }

    puts("waitpid_test: child pid=");
    to_dec(child_pid, num);
    puts(num);
    puts("\n");

    // wait for the child
    puts("waitpid_test: waiting for child...\n");
    int exit_code = waitpid(child_pid);

    puts("waitpid_test: child exited with code=");
    to_dec(exit_code, num);
    puts(num);
    puts("\n");

    if (exit_code == 0)
    {
        puts("waitpid_test: PASS\n");
    }
    else
    {
        puts("waitpid_test: FAIL unexpected exit code\n");
        return 1;
    }

    return 0;
}

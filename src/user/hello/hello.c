/**
 * /bin/hello — first ginnOS user program.
 * prints a message via SYS_write and returns 0.
 */

#define SYS_WRITE 1

static int write(int fd, const char *buf, int count)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(count)
    );
    return ret;
}

static const char msg[] = "hello from /bin/hello!\n";

int main(void)
{
    write(1, msg, sizeof(msg) - 1);
    return 0;
}

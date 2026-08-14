#include "usermode.h"
#include "../syscall/syscall.h"

/**
 * minimal user-mode test function.
 * writes "hello from ring 3!\n" to stdout via SYS_write, then exits via SYS_exit.
 * uses only int 0x80 — no direct kernel calls.
 *
 * this function runs in ring 3 after jump_to_usermode() transfers control.
 */
void __attribute__((section(".text"))) user_test_function(void)
{
    const char msg[] = "hello from ring 3!\n";
    unsigned int len = sizeof(msg) - 1;

    /* SYS_write(fd=1, buf=msg, count=len) */
    __asm__ volatile(
        "mov %0, %%eax\n"
        "mov $1, %%ebx\n"
        "mov %1, %%ecx\n"
        "mov %2, %%edx\n"
        "int $0x80\n"
        :
        : "i"(SYS_WRITE), "r"(msg), "r"(len)
        : "eax", "ebx", "ecx", "edx"
    );

    /* SYS_exit(code=0) */
    __asm__ volatile(
        "mov %0, %%eax\n"
        "mov $0, %%ebx\n"
        "int $0x80\n"
        :
        : "i"(SYS_EXIT)
        : "eax", "ebx"
    );

    /* unreachable, but prevent return into garbage */
    for (;;)
    {
        __asm__ volatile("hlt");
    }
}

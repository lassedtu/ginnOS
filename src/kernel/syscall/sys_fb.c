#include "syscall_internal.h"
#include "syscall.h"

#include "drivers/keyboard/keyboard.h"

/**
 * @file sys_fb.c
 * @brief the pollkey syscall (non-blocking keyboard peek for graphics demos).
 *
 * the interim framebuffer syscalls (SYS_fbinfo/SYS_fbmap) were retired in FB6:
 * userspace now reaches the framebuffer through the /dev/fb0 fbdev
 * (open + ioctl + mmap). pollkey remains until an input-event device (part of
 * the Heimdall input work) replaces it.
 */

/**
 * SYS_pollkey: non-blocking keyboard poll for interactive graphics programs.
 * returns the next pending character-key (a "kbhit"-style peek+consume), or 0
 * if no key is waiting. special keys (arrows etc.) are consumed and reported
 * as 0 so they don't stall the caller.
 * takes no arguments.
 */
int32_t sys_pollkey(struct registers *regs)
{
    (void)regs;

    if (!keyboard_available())
    {
        return 0;
    }

    // a key is ready, so this does not block.
    char c = keyboard_getchar();
    return (int32_t)(unsigned char)c;
}

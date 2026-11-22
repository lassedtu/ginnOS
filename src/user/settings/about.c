/**
 * @file about.c
 * @brief settings: what the system is (uname, the processor, the memory,
 * the screen, the sound card) and the credits.
 */

#include "settings.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fb.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <unistd.h>

static void cpu_name(char *out, size_t size)
{
    uint32_t r[12];
    uint32_t top, b, c, d;
    __asm__ volatile("cpuid" : "=a"(top), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u), "c"(0));
    if (top < 0x80000004u)
    {
        snprintf(out, size, "unknown");
        return;
    }
    for (uint32_t i = 0; i < 3; i++)
        __asm__ volatile("cpuid"
                         : "=a"(r[i * 4]), "=b"(r[i * 4 + 1]), "=c"(r[i * 4 + 2]),
                           "=d"(r[i * 4 + 3])
                         : "a"(0x80000002u + i), "c"(0));
    char brand[49];
    memcpy(brand, r, 48);
    brand[48] = '\0';
    const char *s = brand;
    while (*s == ' ')
        s++;
    snprintf(out, size, "%s", s);
}

idunn_widget_t *about_page(void)
{
    idunn_widget_t *page = idunn_column(6, 0);
    idunn_widget_t *system = page_card(page, "ginnOS", NULL);
    char text[128];
    struct utsname u;
    if (uname(&u) == 0)
    {
        snprintf(text, sizeof(text), "%s %s", u.sysname, u.release);
        fact(system, "Version", text);
        fact(system, "Build", u.version);
        fact(system, "Architecture", u.machine);
    }

    idunn_widget_t *hw = page_card(page, "Hardware", NULL);
    cpu_name(text, sizeof(text));
    fact(hw, "Processor", text);
    char total[32], free_kb[32];
    if (read_field("/proc/meminfo", "MemTotal", total, sizeof(total)) &&
        read_field("/proc/meminfo", "MemFree", free_kb, sizeof(free_kb)))
    {
        snprintf(text, sizeof(text), "%ld MiB, %ld MiB free", atol(total) / 1024,
                 atol(free_kb) / 1024);
        fact(hw, "Memory", text);
    }
    int fb = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
    fb_var_screeninfo_t var;
    if (fb >= 0 && ioctl(fb, FBIOGET_VSCREENINFO, &var) == 0)
    {
        snprintf(text, sizeof(text), "%u x %u", var.xres, var.yres);
        fact(hw, "Screen", text);
    }
    if (fb >= 0)
        close(fb);
    if (read_field("/proc/audio", "Device", text, sizeof(text)) && strcmp(text, "none") != 0)
        fact(hw, "Sound", text);

    idunn_widget_t *credits = page_card(page, "Made with", NULL);
    idunn_add(credits, muted("Inter and JetBrains Mono (SIL Open Font License), the Lucide icons "
                             "(ISC) and the Tela icons (GPL 3).",
                             IDUNN_TEXT_BODY));
    return page;
}

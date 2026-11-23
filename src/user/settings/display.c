/**
 * @file display.c
 * @brief settings: the screen size.
 *
 * with a display driver (virtio-gpu in QEMU) the size changes at once,
 * and the desktop follows. without one the size is the firmware's, set
 * when ginnOS starts: the boot menu's "Screen sizes" folder has the
 * sizes, and this page says so. the list is the kernel's (FBIOGET_MODE).
 */

#include "settings.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MODES_MAX 32

static struct
{
    uint32_t width[MODES_MAX];
    uint32_t height[MODES_MAX];
    char names[MODES_MAX][32];
    int count;
    int current;
    bool driver; // a driver gave the size the screen wants: sizes change at once
    idunn_widget_t *select;
} D;

static const char *const boot_note =
    "This display's resolution is set when ginnOS starts. To change it, restart and choose "
    "a resolution under Screen sizes in the boot menu. The first entry uses the monitor's "
    "preferred resolution, when available.";

static void read_modes(void)
{
    D.count = 0;
    D.current = -1;
    D.driver = false;
    int fd = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    for (uint32_t i = 0; D.count < MODES_MAX; i++)
    {
        fb_mode_t mode = {.index = i};
        if (ioctl(fd, FBIOGET_MODE, &mode) != 0)
            break;
        // the same size at another depth is one choice
        bool seen = false;
        for (int k = 0; k < D.count && !seen; k++)
            seen = D.width[k] == mode.width && D.height[k] == mode.height;
        if (seen)
        {
            if (mode.current)
                for (int k = 0; k < D.count; k++)
                    if (D.width[k] == mode.width && D.height[k] == mode.height)
                        D.current = k;
            continue;
        }
        D.width[D.count] = mode.width;
        D.height[D.count] = mode.height;
        snprintf(D.names[D.count], sizeof(D.names[0]), "%u x %u%s", mode.width, mode.height,
                 mode.preferred ? " (the screen's own)" : "");
        if (mode.current)
            D.current = D.count;
        if (mode.preferred)
            D.driver = true;
        D.count++;
    }
    close(fd);
}

static void size_chosen(idunn_widget_t *select, void *data)
{
    (void)data;
    int i = idunn_select_chosen(select);
    if (i < 0 || i >= D.count || i == D.current)
        return;
    int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    fb_var_screeninfo_t var;
    bool done = false;
    if (fd >= 0 && ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0)
    {
        var.xres = D.width[i];
        var.yres = D.height[i];
        var.bits_per_pixel = 32;
        done = ioctl(fd, FBIOPUT_VSCREENINFO, &var) == 0;
    }
    if (fd >= 0)
        close(fd);
    if (done)
    {
        D.current = i;
        return;
    }
    // no driver: back to the size in use, and how to change it
    idunn_select_choose(select, D.current);
    idunn_widget_t *dialog = idunn_dialog("Resolution cannot be changed", boot_note);
    idunn_dialog_add_button(dialog, "OK", IDUNN_PRIMARY, NULL, NULL);
    idunn_dialog_open(select, dialog);
}

idunn_widget_t *display_page(void)
{
    idunn_widget_t *page = idunn_column(6, 0);
    read_modes();
    idunn_widget_t *card = page_card(page, "Resolution", NULL);
    if (D.count == 0)
    {
        idunn_add(card, muted("No display found.", IDUNN_TEXT_BODY));
        return page;
    }
    const char *names[MODES_MAX];
    for (int i = 0; i < D.count; i++)
        names[i] = D.names[i];
    D.select = idunn_select(names, D.count, D.current, size_chosen, NULL);
    idunn_add(card, idunn_field("Size", D.select,
                                D.driver ? "The resolution changes immediately."
                                         : "Resolutions offered by the firmware."));
    if (!D.driver)
    {
        idunn_widget_t *note = page_card(page, "Other resolutions", NULL);
        idunn_add(note, muted(boot_note, IDUNN_TEXT_BODY));
    }
    return page;
}

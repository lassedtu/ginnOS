/**
 * @file keyboard.c
 * @brief settings: the keyboard layout (the kernel's, kept in keyboard.conf).
 */

#include "settings.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/kbd.h>
#include <unistd.h>

static char layouts[CHOICES_MAX][NAME_LEN];
static int layout_count;

static void layout_chosen(idunn_widget_t *group, void *data)
{
    (void)data;
    uint32_t index = (uint32_t)idunn_radio_group_chosen(group);
    int kbd = open("/dev/kbd", O_RDONLY | O_CLOEXEC);
    if (kbd < 0)
        return;
    if (ioctl(kbd, KBD_IOC_SET_LAYOUT, &index) == 0)
        save_setting("keyboard", "keyboard", "layout", layouts[index]);
    close(kbd);
}

idunn_widget_t *keyboard_page(void)
{
    idunn_widget_t *page = idunn_column(6, 0);
    idunn_widget_t *card = page_card(page, "Layout",
                                   "The keys type the characters of this layout, on the "
                                   "desktop and in the text console.");
    const char *names[CHOICES_MAX];
    uint32_t count = 0;
    uint32_t active = 0;
    layout_count = 0;
    int kbd = open("/dev/kbd", O_RDONLY | O_CLOEXEC);
    if (kbd >= 0 && ioctl(kbd, KBD_IOC_LAYOUT_COUNT, &count) == 0 &&
        ioctl(kbd, KBD_IOC_GET_LAYOUT, &active) == 0)
    {
        for (uint32_t i = 0; i < count && layout_count < CHOICES_MAX; i++)
        {
            struct kbd_layout_name name = {.index = i};
            if (ioctl(kbd, KBD_IOC_LAYOUT_NAME, &name) != 0)
                break;
            snprintf(layouts[layout_count], NAME_LEN, "%s", name.name);
            names[layout_count] = layouts[layout_count];
            layout_count++;
        }
    }
    if (kbd >= 0)
        close(kbd);
    if (layout_count == 0)
    {
        idunn_add(card, muted("There is no keyboard device.", IDUNN_TEXT_BODY));
        return page;
    }
    idunn_add(card, idunn_radio_group(names, layout_count, (int)active, layout_chosen, NULL));

    idunn_widget_t *test = page_card(page, "Try it", NULL);
    idunn_add(test, idunn_text_field("Type here to try the layout"));
    return page;
}

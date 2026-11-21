/**
 * @file kbdlayout.c
 * @brief list the keyboard layouts, or switch to one.
 *
 * usage: kbdlayout          list the layouts; the active one has a *
 *        kbdlayout <name>   make <name> the active layout
 *
 * the layout is for the whole system and lasts until the next boot.
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/kbd.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int fd = open("/dev/kbd", O_RDONLY);
    uint32_t count;
    uint32_t active;
    if (fd < 0 || ioctl(fd, KBD_IOC_LAYOUT_COUNT, &count) != 0 ||
        ioctl(fd, KBD_IOC_GET_LAYOUT, &active) != 0)
    {
        printf("kbdlayout: no keyboard\n");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        struct kbd_layout_name info = {.index = i};
        if (ioctl(fd, KBD_IOC_LAYOUT_NAME, &info) != 0)
        {
            continue;
        }
        if (argc < 2)
        {
            printf("%c %s\n", i == active ? '*' : ' ', info.name);
        }
        else if (strcmp(info.name, argv[1]) == 0)
        {
            if (ioctl(fd, KBD_IOC_SET_LAYOUT, &i) != 0)
            {
                printf("kbdlayout: could not set %s\n", argv[1]);
                return 1;
            }
            return 0;
        }
    }
    if (argc >= 2)
    {
        printf("kbdlayout: no layout named %s\n", argv[1]);
        return 1;
    }
    return 0;
}

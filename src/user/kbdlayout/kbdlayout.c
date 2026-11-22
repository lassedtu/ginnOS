/**
 * @file kbdlayout.c
 * @brief list the keyboard layouts, or switch to one.
 *
 * usage: kbdlayout          list the layouts; the active one has a *
 *        kbdlayout <name>   make <name> the active layout
 *        kbdlayout -k       show the active layout's characters, as the
 *                           window server gets them (KBD_IOC_GET_KEYMAP)
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

/**
 * print one table of the keymap: the characters of the key rows.
 */
static void print_table(const char *name, const char *table)
{
    // the key codes of the number row, the three letter rows and the ISO key
    static const uint8_t rows[][2] = {{0x02, 0x0D}, {0x10, 0x1B}, {0x1E, 0x29}, {0x2B, 0x35}};
    printf("%-12s", name);
    for (int r = 0; r < 4; r++)
    {
        for (int k = rows[r][0]; k <= rows[r][1]; k++)
            putchar(table[k] >= ' ' && table[k] < 0x7F ? table[k] : '.');
        putchar(' ');
    }
    putchar(table[0x56] >= ' ' && table[0x56] < 0x7F ? table[0x56] : '.');
    putchar('\n');
}

static int show_keymap(int fd)
{
    struct kbd_keymap map;
    if (ioctl(fd, KBD_IOC_GET_KEYMAP, &map) != 0)
    {
        printf("kbdlayout: cannot read the keymap\n");
        return 1;
    }
    print_table("normal", map.normal);
    print_table("shift", map.shift);
    print_table("altgr", map.altgr);
    print_table("altgr+shift", map.altgr_shift);
    return 0;
}

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

    if (argc >= 2 && strcmp(argv[1], "-k") == 0)
        return show_keymap(fd);

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

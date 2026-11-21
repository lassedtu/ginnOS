/**
 * @file evtest.c
 * @brief print the events of /dev/input.
 *
 * usage: evtest [count]   print events (stop after count of them; Ctrl+C
 *                          stops it too)
 *        evtest grab [n]   the same, holding the keyboard: keys do not reach
 *                          the shell, and Escape stops it
 *
 * a line per event: the time, then the event by name, for example
 * "1234 REL_X -3" or "1240 BTN_LEFT down".
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>
#include <unistd.h>

static const char *rel_name(int code)
{
    switch (code)
    {
    case REL_X:
        return "REL_X";
    case REL_Y:
        return "REL_Y";
    case REL_WHEEL:
        return "REL_WHEEL";
    default:
        return "REL_?";
    }
}

static const char *button_name(int code)
{
    switch (code)
    {
    case BTN_LEFT:
        return "BTN_LEFT";
    case BTN_RIGHT:
        return "BTN_RIGHT";
    case BTN_MIDDLE:
        return "BTN_MIDDLE";
    default:
        return NULL;
    }
}

static void print_event(const input_event_t *e)
{
    static const char *const states[] = {"up", "down", "repeat"};
    const char *state = e->value >= 0 && e->value <= 2 ? states[e->value] : "?";

    switch (e->type)
    {
    case EV_SYN:
        if (e->code == SYN_DROPPED)
            printf("%u SYN_DROPPED\n", e->time_ms);
        break;
    case EV_KEY:
        if (button_name(e->code))
            printf("%u %s %s\n", e->time_ms, button_name(e->code), state);
        else
            printf("%u KEY %d %s\n", e->time_ms, e->code, state);
        break;
    case EV_REL:
        printf("%u %s %d\n", e->time_ms, rel_name(e->code), e->value);
        break;
    case EV_ABS:
        printf("%u %s %d\n", e->time_ms, e->code == ABS_X ? "ABS_X" : "ABS_Y", e->value);
        break;
    default:
        printf("%u type %d code %d value %d\n", e->time_ms, e->type, e->code, e->value);
        break;
    }
}

int main(int argc, char **argv)
{
    int grab = argc > 1 && strcmp(argv[1], "grab") == 0;
    const char *count_arg = argc > 1 + grab ? argv[1 + grab] : NULL;
    long limit = count_arg ? atol(count_arg) : 0;

    int fd = open("/dev/input", O_RDONLY);
    if (fd < 0)
    {
        printf("evtest: cannot open /dev/input\n");
        return 1;
    }
    if (grab)
    {
        int on = 1;
        ioctl(fd, INPUT_IOC_GRAB, &on);
        printf("evtest: the keyboard is grabbed; Escape stops\n");
    }

    long printed = 0;
    input_event_t events[16];
    for (;;)
    {
        ssize_t n = read(fd, events, sizeof(events));
        if (n <= 0)
            break;
        for (ssize_t i = 0; i < n / (ssize_t)sizeof(input_event_t); i++)
        {
            const input_event_t *e = &events[i];
            if (grab && e->type == EV_KEY && e->code == KEY_ESC)
            {
                close(fd);
                return 0;
            }
            if (e->type == EV_SYN && e->code == SYN_REPORT)
                continue;
            print_event(e);
            if (limit && ++printed >= limit)
            {
                close(fd);
                return 0;
            }
        }
    }
    close(fd);
    return 0;
}

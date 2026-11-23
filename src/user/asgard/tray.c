/**
 * @file tray.c
 * @brief asgard: the right of the panel: the keyboard layout, the volume,
 *        and the clock with a calendar of the month.
 */

#include "asgard.h"

#include <conf.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <time.h>
#include <unistd.h>

#define DAY_WIDTH 32 // design pixels: a day of the calendar

/**
 * the short name of a layout for the panel: "dk-mac" shows as "DK".
 */
static void layout_short(const char *name, char out[4])
{
    int n = 0;
    while (n < 3 && name[n] && name[n] != '-' && name[n] != '_')
    {
        out[n] = (char)(name[n] >= 'a' && name[n] <= 'z' ? name[n] - 'a' + 'A' : name[n]);
        n++;
    }
    out[n] = '\0';
}

static void layout_chosen(idunn_widget_t *owner, void *data)
{
    (void)owner;
    uint32_t index = (uint32_t)(uintptr_t)data;
    int kbd = open("/dev/kbd", O_RDONLY | O_CLOEXEC);
    if (kbd < 0)
        return;
    if (ioctl(kbd, KBD_IOC_SET_LAYOUT, &index) == 0)
    {
        char text[4];
        layout_short(A.layout_names[index], text);
        idunn_button_set_text(A.layout, text);
        for (int i = 0; i < A.layout_count; i++)
            idunn_menu_set_checked(A.layout_menu, i, (uint32_t)i == index);
        // kept for the next session (heimdalld makes it active at the start)
        conf_t *conf = conf_open("keyboard");
        if (conf)
        {
            conf_set(conf, "keyboard", "layout", A.layout_names[index]);
            conf_save(conf);
            conf_free(conf);
        }
        heim_settings_changed(A.conn, "keyboard"); // heimdalld reads it again
    }
    close(kbd);
}

static void open_layouts(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_menu_popup(A.layout_menu, button);
}

idunn_widget_t *layout_button(void)
{
    A.layout_menu = idunn_menu_new();
    A.layout = idunn_button("", IDUNN_GHOST, open_layouts, NULL);
    idunn_button_set_icon(A.layout, "keyboard");
    idunn_set_tooltip(A.layout, "Keyboard layout");
    int kbd = open("/dev/kbd", O_RDONLY | O_CLOEXEC);
    uint32_t count = 0;
    uint32_t active = 0;
    if (kbd < 0 || ioctl(kbd, KBD_IOC_LAYOUT_COUNT, &count) != 0 ||
        ioctl(kbd, KBD_IOC_GET_LAYOUT, &active) != 0)
        count = 0;
    for (uint32_t i = 0; i < count && i < LAYOUTS_MAX; i++)
    {
        struct kbd_layout_name name = {.index = i};
        if (ioctl(kbd, KBD_IOC_LAYOUT_NAME, &name) != 0)
            break;
        snprintf(A.layout_names[i], KBD_LAYOUT_NAME_MAX, "%s", name.name);
        idunn_menu_add_check(A.layout_menu, A.layout_names[i], i == active, layout_chosen,
                             (void *)(uintptr_t)i);
        A.layout_count++;
    }
    if (kbd >= 0)
        close(kbd);
    if (A.layout_count == 0)
        idunn_set_hidden(A.layout, true);
    else
    {
        char text[4];
        layout_short(A.layout_names[active < (uint32_t)A.layout_count ? active : 0], text);
        idunn_button_set_text(A.layout, text);
    }
    return A.layout;
}

/**
 * the volume of /dev/mixer, 0 to 100 (both channels the same), or -1.
 */
static int read_volume(void)
{
    int mixer = open("/dev/mixer", O_RDONLY | O_CLOEXEC);
    int value = -1;
    if (mixer >= 0 && ioctl(mixer, SOUND_MIXER_READ_VOLUME, &value) != 0)
        value = -1;
    if (mixer >= 0)
        close(mixer);
    return value < 0 ? -1 : value & 0xFF;
}

static const char *volume_icon(int level)
{
    return level == 0 ? "volume-x" : level < 50 ? "volume-1" : "volume-2";
}

static void volume_changed(idunn_widget_t *slider, void *data)
{
    idunn_widget_t *value_label = data;
    int level = idunn_slider_value(slider);
    int value = level | (level << 8); // left and right
    int mixer = open("/dev/mixer", O_RDWR | O_CLOEXEC);
    if (mixer >= 0)
    {
        ioctl(mixer, SOUND_MIXER_WRITE_VOLUME, &value);
        close(mixer);
    }
    idunn_button_set_icon(A.volume, volume_icon(level));
    char text[8];
    snprintf(text, sizeof(text), "%d %%", level);
    idunn_label_set_text(value_label, text);
}

static void open_volume(idunn_widget_t *button, void *data)
{
    (void)data;
    int level = read_volume();
    if (level < 0)
        level = 0;
    idunn_widget_t *content = idunn_column(3, 1);
    idunn_widget_t *head = idunn_add(content, idunn_row(2, 0));
    idunn_box_set_align(head, IDUNN_CENTER, IDUNN_SPACE_BETWEEN);
    idunn_add(head, idunn_label("Volume", IDUNN_TEXT_LABEL));
    char text[8];
    snprintf(text, sizeof(text), "%d %%", level);
    idunn_widget_t *value = idunn_add(head, idunn_label(text, IDUNN_TEXT_SMALL));
    idunn_label_set_color(value, IDUNN_COLOR_MUTED_FOREGROUND);
    idunn_widget_t *slider = idunn_add(content, idunn_slider(0, 100, 5, level, volume_changed,
                                                             value));
    idunn_set_size(slider, 220, 0);
    idunn_popover(button, content);
}

idunn_widget_t *volume_button(void)
{
    int level = read_volume();
    A.volume = idunn_icon_button(volume_icon(level < 0 ? 100 : level), IDUNN_GHOST, open_volume,
                                 NULL);
    idunn_set_tooltip(A.volume, "Volume");
    if (level < 0)
        idunn_set_hidden(A.volume, true);
    return A.volume;
}

static struct
{
    idunn_widget_t *title;
    idunn_widget_t *days; // the rows of days, made again for another month
    idunn_widget_t *body;
    int year;
    int month; // 0 to 11
} cal;

static int days_in(int year, int month)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 1 && leap ? 29 : days[month];
}

/** the weekday of the first of the month, Monday 0 */
static int first_weekday(int year, int month)
{
    struct tm tm = {.tm_year = year - 1900, .tm_mon = month, .tm_mday = 1, .tm_hour = 12};
    mktime(&tm);
    return (tm.tm_wday + 6) % 7;
}

static void show_month(void)
{
    static const char *const months[12] = {"January", "February", "March",     "April",
                                           "May",     "June",     "July",      "August",
                                           "September", "October", "November", "December"};
    char title[32];
    snprintf(title, sizeof(title), "%s %d", months[cal.month], cal.year);
    idunn_label_set_text(cal.title, title);

    time_t now = time(NULL);
    struct tm today;
    localtime_r(&now, &today);
    bool this_month = today.tm_year + 1900 == cal.year && today.tm_mon == cal.month;

    idunn_widget_free(cal.days);
    cal.days = idunn_add(cal.body, idunn_column(1, 0));
    int first = first_weekday(cal.year, cal.month);
    int count = days_in(cal.year, cal.month);
    idunn_widget_t *row = NULL;
    for (int cell = 0; cell < first + count; cell++)
    {
        if (cell % 7 == 0)
            row = idunn_add(cal.days, idunn_row(1, 0));
        idunn_widget_t *w;
        if (cell < first)
            w = idunn_add(row, idunn_label(" ", IDUNN_TEXT_SMALL));
        else
        {
            int day = cell - first + 1;
            char text[4];
            snprintf(text, sizeof(text), "%d", day);
            bool is_today = this_month && day == today.tm_mday;
            w = idunn_add(row, idunn_button(text, is_today ? IDUNN_PRIMARY : IDUNN_GHOST, NULL,
                                            NULL));
            idunn_button_set_size(w, IDUNN_SIZE_SMALL);
        }
        idunn_set_size(w, DAY_WIDTH, 28);
    }
}

static void step_month(idunn_widget_t *button, void *data)
{
    (void)button;
    cal.month += (int)(intptr_t)data;
    if (cal.month < 0)
    {
        cal.month = 11;
        cal.year--;
    }
    else if (cal.month > 11)
    {
        cal.month = 0;
        cal.year++;
    }
    show_month();
}

static void open_calendar(idunn_widget_t *button, void *data)
{
    (void)data;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    cal.year = tm.tm_year + 1900;
    cal.month = tm.tm_mon;

    idunn_widget_t *content = idunn_column(2, 1);
    char day[64];
    strftime(day, sizeof(day), "%A %d %B", &tm);
    idunn_add(content, idunn_label(day, IDUNN_TEXT_LABEL));
    idunn_add(content, idunn_separator());

    idunn_widget_t *head = idunn_add(content, idunn_row(1, 0));
    idunn_box_set_align(head, IDUNN_CENTER, IDUNN_SPACE_BETWEEN);
    idunn_widget_t *back = idunn_add(head, idunn_icon_button("chevron-left", IDUNN_GHOST,
                                                             step_month, (void *)-1));
    idunn_button_set_size(back, IDUNN_SIZE_SMALL);
    cal.title = idunn_add(head, idunn_label(" ", IDUNN_TEXT_LABEL));
    idunn_widget_t *next = idunn_add(head, idunn_icon_button("chevron-right", IDUNN_GHOST,
                                                             step_month, (void *)1));
    idunn_button_set_size(next, IDUNN_SIZE_SMALL);

    cal.body = idunn_add(content, idunn_column(1, 0));
    idunn_widget_t *names = idunn_add(cal.body, idunn_row(1, 0));
    static const char *const weekdays[7] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    for (int i = 0; i < 7; i++)
    {
        idunn_widget_t *cell = idunn_add(names, idunn_row(0, 0));
        idunn_box_set_align(cell, IDUNN_CENTER, IDUNN_CENTER);
        idunn_set_size(cell, DAY_WIDTH, 20);
        idunn_widget_t *name = idunn_add(cell, idunn_label(weekdays[i], IDUNN_TEXT_SMALL));
        idunn_label_set_color(name, IDUNN_COLOR_MUTED_FOREGROUND);
    }
    cal.days = NULL;
    show_month();
    idunn_popover(button, content);
}

static int clock_minute = -1; // the minute the clock shows

void clock_tick(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_min == clock_minute)
        return;
    clock_minute = tm.tm_min;
    char text[32];
    strftime(text, sizeof(text), "%a %d %b   %H:%M", &tm);
    idunn_button_set_text(A.clock, text);
    strftime(text, sizeof(text), "%A %d %B %Y", &tm);
    idunn_set_tooltip(A.clock, text);
}

idunn_widget_t *clock_button(void)
{
    A.clock = idunn_button("", IDUNN_GHOST, open_calendar, NULL);
    clock_tick();
    return A.clock;
}

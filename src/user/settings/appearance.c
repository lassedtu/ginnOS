/**
 * @file appearance.c
 * @brief settings: the theme (theme.conf) and the wallpaper (desktop.conf).
 */

#include "settings.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#define WALLPAPERS "/usr/share/wallpapers"

static char wallpapers[CHOICES_MAX][NAME_LEN]; // the paths; [0] is "none"
static int wallpaper_count;

static const char *const modes[] = {"light", "dark"};
static const char *const mode_names[] = {"Light", "Dark"};
static const char *const accents[] = {"rose", "blue", "green", "orange", "violet", "neutral"};
static const char *const accent_names[] = {"Rose", "Blue", "Green", "Orange", "Violet", "Neutral"};
static const char *const sizes[] = {"13", "14", "15"};
static const char *const size_names[] = {"Small (13)", "Medium (14)", "Large (15)"};
static const char *const scales[] = {"auto", "1", "2"};
static const char *const scale_names[] = {"Automatic", "1x", "2x"};

static void mode_chosen(idunn_widget_t *group, void *data)
{
    (void)data;
    idunn_app_save_theme(settings_app, "mode", modes[idunn_radio_group_chosen(group)]);
}

static void accent_chosen(idunn_widget_t *select, void *data)
{
    (void)data;
    idunn_app_save_theme(settings_app, "accent", accents[idunn_select_chosen(select)]);
}

static void size_chosen(idunn_widget_t *group, void *data)
{
    (void)data;
    idunn_app_save_theme(settings_app, "font_size", sizes[idunn_radio_group_chosen(group)]);
}

static void scale_chosen(idunn_widget_t *select, void *data)
{
    (void)data;
    idunn_app_save_theme(settings_app, "scale", scales[idunn_select_chosen(select)]);
}

static void motion_changed(idunn_widget_t *sw, void *data)
{
    (void)data;
    idunn_app_save_theme(settings_app, "motion", idunn_switch_on(sw) ? "true" : "false");
}

static void wallpaper_chosen(idunn_widget_t *select, void *data)
{
    (void)data;
    int i = idunn_select_chosen(select);
    if (i >= 0 && i < wallpaper_count)
        save_setting("desktop", "desktop", "wallpaper", wallpapers[i]);
}

/** the pictures in /usr/share/wallpapers, after "none" */
static int list_wallpapers(const char *names[CHOICES_MAX])
{
    wallpaper_count = 0;
    snprintf(wallpapers[wallpaper_count], NAME_LEN, "none");
    names[wallpaper_count++] = "None (gradient)";
    DIR *dir = opendir(WALLPAPERS);
    struct dirent *e;
    while (dir && (e = readdir(dir)) && wallpaper_count < CHOICES_MAX)
    {
        const char *dot = strrchr(e->d_name, '.');
        if (e->d_name[0] == '.' || !dot ||
            (strcmp(dot, ".png") != 0 && strcmp(dot, ".jpg") != 0 && strcmp(dot, ".bmp") != 0))
            continue;
        snprintf(wallpapers[wallpaper_count], NAME_LEN, WALLPAPERS "/%s", e->d_name);
        names[wallpaper_count] = strrchr(wallpapers[wallpaper_count], '/') + 1;
        wallpaper_count++;
    }
    if (dir)
        closedir(dir);
    return wallpaper_count;
}

idunn_widget_t *appearance_page(void)
{
    idunn_widget_t *page = idunn_column(6, 0);
    char value[NAME_LEN];

    idunn_widget_t *look = page_card(page, "Theme", "Changes apply to all open windows.");
    read_setting("theme", "theme", "mode", "dark", value, sizeof(value));
    int mode = index_of(modes, 2, value);
    idunn_widget_t *mode_group = idunn_radio_group(mode_names, 2, mode < 0 ? 1 : mode,
                                                   mode_chosen, NULL);
    idunn_radio_group_set_row(mode_group, true);
    idunn_add(look, idunn_field("Mode", mode_group, NULL));
    read_setting("theme", "theme", "accent", "rose", value, sizeof(value));
    int accent = index_of(accents, 6, value);
    idunn_widget_t *accent_select = idunn_select(accent_names, 6, accent, accent_chosen, NULL);
    if (accent < 0)
        idunn_select_set_placeholder(accent_select, value); // a colour of the user's own
    idunn_add(look, idunn_field("Accent colour", accent_select,
                                "Used for buttons, selections and focus highlights."));

    idunn_widget_t *text = page_card(page, "Text and scaling", NULL);
    read_setting("theme", "theme", "font_size", "13", value, sizeof(value));
    int size = index_of(sizes, 3, value);
    idunn_widget_t *size_group = idunn_radio_group(size_names, 3, size < 0 ? 0 : size,
                                                   size_chosen, NULL);
    idunn_radio_group_set_row(size_group, true);
    idunn_add(text, idunn_field("Text size", size_group, NULL));
    read_setting("theme", "theme", "scale", "auto", value, sizeof(value));
    int scale = index_of(scales, 3, value);
    idunn_add(text, idunn_field("Scale",
                                idunn_select(scale_names, 3, scale < 0 ? 0 : scale, scale_chosen,
                                             NULL),
                                "2x doubles the size of everything, for high-density screens. "
                                "Automatic uses 2x on screens 2400 pixels wide or wider."));
    read_setting("theme", "theme", "motion", "true", value, sizeof(value));
    idunn_add(text, idunn_switch("Animations", strcmp(value, "false") != 0, motion_changed,
                                 NULL));

    idunn_widget_t *desk = page_card(page, "Wallpaper", "The desktop background.");
    const char *names[CHOICES_MAX];
    int count = list_wallpapers(names);
    read_setting("desktop", "desktop", "wallpaper", WALLPAPERS "/default.png", value,
                 sizeof(value));
    int chosen = 0;
    for (int i = 0; i < count; i++)
        if (strcmp(wallpapers[i], value) == 0)
            chosen = i;
    idunn_add(desk, idunn_field("Image",
                                idunn_select(names, count, chosen, wallpaper_chosen, NULL),
                                "To add wallpapers, copy PNG, JPEG or BMP files to "
                                WALLPAPERS "."));
    return page;
}

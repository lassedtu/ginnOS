/**
 * @file sound.c
 * @brief settings: the volume of the sound card (/dev/mixer).
 */

#include "settings.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <unistd.h>

static idunn_widget_t *volume_value; // the volume as a number, next to the slider

/** the volume of /dev/mixer, 0 to 100, or -1 without a sound card */
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

static void volume_changed(idunn_widget_t *slider, void *data)
{
    (void)data;
    int level = idunn_slider_value(slider);
    int value = level | (level << 8);
    int mixer = open("/dev/mixer", O_RDWR | O_CLOEXEC);
    if (mixer >= 0)
    {
        ioctl(mixer, SOUND_MIXER_WRITE_VOLUME, &value);
        close(mixer);
    }
    char text[16];
    snprintf(text, sizeof(text), "%d%%", level);
    idunn_label_set_text(volume_value, text);
}

idunn_widget_t *sound_page(void)
{
    idunn_widget_t *page = idunn_column(6, 0);
    int level = read_volume();
    if (level < 0)
    {
        idunn_add(page, idunn_empty_state("volume-x", "No sound card found",
                                          "ginnOS knows the HDA and AC'97 sound cards."));
        return page;
    }
    char device[96] = "Sound card";
    read_field("/proc/audio", "Device", device, sizeof(device));
    idunn_widget_t *card = page_card(page, "Output", device);
    idunn_widget_t *row = idunn_add(card, idunn_row(3, 0));
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
    idunn_add(row, idunn_label("Volume", IDUNN_TEXT_LABEL));
    idunn_grow(idunn_add(row, idunn_slider(0, 100, 5, level, volume_changed, NULL)), 1);
    char text[16];
    snprintf(text, sizeof(text), "%d%%", level);
    volume_value = idunn_add(row, muted(text, IDUNN_TEXT_BODY));
    idunn_set_size(volume_value, 40, 0);
    return page;
}

/**
 * @file doomgeneric_ginnos_sound.c
 * @brief Doom sound effects on /dev/dsp (the doomgeneric sound module).
 *
 * Doom's sound effects are lumps in the WAD (DMX format: a small header,
 * then unsigned 8-bit mono samples, usually at 11025 Hz). the engine
 * starts a sound on one of its channels (8 by default) with a volume and
 * a stereo separation, and calls Update about once per frame.
 *
 * Update mixes all channels into 16-bit stereo at the device rate (each
 * sound is stepped through in 16.16 fixed point, so any sample rate
 * works) and writes it without blocking, so that about TARGET_MS of
 * sound is queued in the kernel. a slow frame then plays from the queue
 * instead of breaking up.
 *
 * music: Chocolate Doom's OPL music player (music_opl_module) plays the
 * MUS songs on an emulated OPL3 chip (doomgeneric_ginnos_opl.c). its
 * samples are made at the device rate and added to the effects in the
 * same mix.
 */

#include "doomgeneric_ginnos_opl.h"
#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

// unistd.h clashes with Doom's headers (see doomgeneric_ginnos.c)
long write(int fd, const void *buf, unsigned long count);
int close(int fd);

#define CHANNELS   16       // more than the engine's 8, for a changed snd_channels
#define TARGET_MS  100      // sound kept queued in the kernel
#define MIX_FRAMES 4096     // the most frames mixed per call
#define DMX_HEADER 8        // format, rate, sample count
#define DMX_PAD    16       // DMX pads the samples with 16 bytes at each end

// the engine declares these for its configuration file (libsamplerate,
// which ginnOS does not have)
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

typedef struct
{
    const uint8_t *samples; // NULL: the channel is free
    uint32_t length;        // samples
    uint32_t position;      // 16.16 fixed point
    uint32_t step;          // 16.16: sound rate / device rate
    int32_t left;           // 0 to 254
    int32_t right;
} channel_t;

static int dsp = -1;
static int device_rate;
static boolean sfx_prefix;
static channel_t channels[CHANNELS];
static int16_t mix_buffer[MIX_FRAMES * 2];
static int16_t music_buffer[MIX_FRAMES * 2];

// the rate the music player gives the OPL emulator (i_sound.c)
extern int snd_samplerate;

/**
 * the lump of a sound: the linked sound's, with the "ds" prefix in Doom.
 */
static int sfx_lump(sfxinfo_t *sfx)
{
    char name[16];
    if (sfx->link != NULL)
    {
        sfx = sfx->link;
    }
    snprintf(name, sizeof(name), sfx_prefix ? "ds%s" : "%s", sfx->name);
    return W_CheckNumForName(name);
}

static boolean sound_init(boolean use_sfx_prefix)
{
    sfx_prefix = use_sfx_prefix;
    memset(channels, 0, sizeof(channels));

    dsp = open("/dev/dsp", O_WRONLY | O_NONBLOCK);
    if (dsp < 0)
    {
        return false; // no sound device, or it is in use: play without sound
    }
    int format = AFMT_S16_LE;
    int stereo = 2;
    device_rate = 44100;
    if (ioctl(dsp, SNDCTL_DSP_SETFMT, &format) < 0 || format != AFMT_S16_LE ||
        ioctl(dsp, SNDCTL_DSP_CHANNELS, &stereo) < 0 || stereo != 2 ||
        ioctl(dsp, SNDCTL_DSP_SPEED, &device_rate) < 0 || device_rate <= 0)
    {
        close(dsp);
        dsp = -1;
        return false;
    }
    snd_samplerate = device_rate; // the music is made at the device rate
    return true;
}

static void sound_shutdown(void)
{
    if (dsp >= 0)
    {
        close(dsp);
        dsp = -1;
    }
}

static int sound_get_lump(sfxinfo_t *sfx)
{
    return sfx_lump(sfx);
}

/**
 * the volume and separation (0 left, 128 centre, 254 right) as the left
 * and right gains (0 to 254).
 */
static void set_params(channel_t *channel, int vol, int sep)
{
    if (sep < 0)
    {
        sep = 0;
    }
    if (sep > 254)
    {
        sep = 254;
    }
    channel->left = (254 - sep) * vol / 127;
    channel->right = sep * vol / 127;
}

static void sound_update_params(int channel, int vol, int sep)
{
    if (channel >= 0 && channel < CHANNELS)
    {
        set_params(&channels[channel], vol, sep);
    }
}

static int sound_start(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    if (dsp < 0 || channel < 0 || channel >= CHANNELS)
    {
        return -1;
    }

    int lump = sfx->lumpnum >= 0 ? sfx->lumpnum : sfx_lump(sfx);
    if (lump < 0)
    {
        return -1;
    }
    const uint8_t *data = W_CacheLumpNum(lump, PU_STATIC);
    uint32_t size = (uint32_t)W_LumpLength(lump);
    if (size < DMX_HEADER || data[0] != 3 || data[1] != 0)
    {
        return -1; // not a digital sound lump
    }

    uint32_t rate = (uint32_t)(data[2] | (data[3] << 8));
    uint32_t length = (uint32_t)data[4] | ((uint32_t)data[5] << 8) | ((uint32_t)data[6] << 16) |
                      ((uint32_t)data[7] << 24);
    if (length > size - DMX_HEADER)
    {
        length = size - DMX_HEADER;
    }
    const uint8_t *samples = data + DMX_HEADER;
    if (length > 2 * DMX_PAD + 16)
    {
        samples += DMX_PAD;
        length -= 2 * DMX_PAD;
    }
    if (rate == 0 || length == 0)
    {
        return -1;
    }

    channel_t *c = &channels[channel];
    c->samples = samples;
    c->length = length;
    c->position = 0;
    c->step = (uint32_t)(((uint64_t)rate << 16) / (uint32_t)device_rate);
    set_params(c, vol, sep);
    return channel;
}

static void sound_stop(int channel)
{
    if (channel >= 0 && channel < CHANNELS)
    {
        channels[channel].samples = NULL;
    }
}

static boolean sound_is_playing(int channel)
{
    return channel >= 0 && channel < CHANNELS && channels[channel].samples != NULL;
}

static int32_t clamp16(int32_t value)
{
    return value > 32767 ? 32767 : (value < -32768 ? -32768 : value);
}

/**
 * mix the channels into the buffer.
 */
static void mix(int frames)
{
    opl_ginnos_generate(music_buffer, (unsigned int)frames);

    for (int i = 0; i < frames; i++)
    {
        int32_t left = music_buffer[i * 2];
        int32_t right = music_buffer[i * 2 + 1];
        for (int n = 0; n < CHANNELS; n++)
        {
            channel_t *c = &channels[n];
            if (c->samples == NULL)
            {
                continue;
            }
            uint32_t index = c->position >> 16;
            if (index >= c->length)
            {
                c->samples = NULL; // the sound has ended
                continue;
            }
            int32_t sample = ((int32_t)c->samples[index] - 128) * 256;
            left += sample * c->left / 254;
            right += sample * c->right / 254;
            c->position += c->step;
        }
        mix_buffer[i * 2] = (int16_t)clamp16(left);
        mix_buffer[i * 2 + 1] = (int16_t)clamp16(right);
    }
}

static void sound_update(void)
{
    if (dsp < 0)
    {
        return;
    }

    // frames to add: up to TARGET_MS queued
    int queued_bytes = 0;
    if (ioctl(dsp, SNDCTL_DSP_GETODELAY, &queued_bytes) < 0)
    {
        return;
    }
    int target = device_rate * TARGET_MS / 1000;
    int frames = target - queued_bytes / 4;
    if (frames <= 0)
    {
        return;
    }
    if (frames > MIX_FRAMES)
    {
        frames = MIX_FRAMES;
    }

    mix(frames);
    write(dsp, mix_buffer, (unsigned long)frames * 4); // a short write drops the rest
}

static void sound_cache(sfxinfo_t *sounds, int count)
{
    // the lumps are read when a sound first plays; only find them here
    for (int i = 0; i < count; i++)
    {
        sounds[i].lumpnum = sfx_lump(&sounds[i]);
    }
}

static snddevice_t sound_devices[] = {
    SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module = {
    sound_devices,
    sizeof(sound_devices) / sizeof(sound_devices[0]),
    sound_init,
    sound_shutdown,
    sound_get_lump,
    sound_update,
    sound_update_params,
    sound_start,
    sound_stop,
    sound_is_playing,
    sound_cache,
};

// music: Chocolate Doom's OPL music player

extern music_module_t music_opl_module;

static boolean music_init(void)
{
    if (dsp < 0)
    {
        return false; // no sound device: the samples would go nowhere
    }
    return music_opl_module.Init();
}

static void music_shutdown(void)
{
    music_opl_module.Shutdown();
}

static void music_set_volume(int volume)
{
    music_opl_module.SetMusicVolume(volume);
}

static void music_pause(void)
{
    music_opl_module.PauseMusic();
}

static void music_resume(void)
{
    music_opl_module.ResumeMusic();
}

static void *music_register(void *data, int len)
{
    return music_opl_module.RegisterSong(data, len);
}

static void music_unregister(void *handle)
{
    music_opl_module.UnRegisterSong(handle);
}

static void music_play(void *handle, boolean looping)
{
    music_opl_module.PlaySong(handle, looping);
}

static void music_stop(void)
{
    music_opl_module.StopSong();
}

static boolean music_is_playing(void)
{
    return music_opl_module.MusicIsPlaying();
}

music_module_t DG_music_module = {
    sound_devices,
    sizeof(sound_devices) / sizeof(sound_devices[0]),
    music_init,
    music_shutdown,
    music_set_volume,
    music_pause,
    music_resume,
    music_register,
    music_unregister,
    music_play,
    music_stop,
    music_is_playing,
    NULL,
};

#pragma once

/**
 * @file audio.h
 * @brief the sound output core: /dev/dsp, /dev/mixer and the driver
 * interface.
 *
 * a program writes PCM to /dev/dsp. the core converts it to the device
 * format (signed 16-bit little-endian stereo, at the device rate) and
 * keeps it in a ring buffer. the driver plays from DMA buffers split in
 * periods; when the device has played a period, the driver interrupt
 * calls audio_pull to fill it again. if the ring is empty, the period is
 * filled with silence and counted as an underrun (/proc/audio).
 *
 * one program at a time opens /dev/dsp (EBUSY for a second one); a
 * sound server can share it later.
 */

#include "common/stdint.h"

typedef struct audio_device audio_device_t;

typedef struct
{
    /**
     * start playing. the driver fills all its periods with audio_pull
     * first. called in process context.
     * @return false if the device cannot start.
     */
    bool (*start)(audio_device_t *dev);

    /**
     * stop playing. called in process context.
     */
    void (*stop)(audio_device_t *dev);

    /**
     * use a sample rate, or the nearest one the device has. called only
     * while the device is stopped.
     * @param rate the wanted rate in Hz.
     * @return the rate in use.
     */
    uint32_t (*set_rate)(audio_device_t *dev, uint32_t rate);

    /**
     * set the output volume (optional).
     * @param left the left volume, 0 to 100.
     * @param right the right volume, 0 to 100.
     */
    void (*set_volume)(audio_device_t *dev, uint32_t left, uint32_t right);
} audio_ops_t;

struct audio_device
{
    const char *name;          // the driver ("ac97", "hda")
    const audio_ops_t *ops;
    void *driver_data;
    uint32_t rate;             // the rate in use (set by the driver at register)
    uint32_t period_bytes;     // bytes per DMA period
    uint32_t periods;          // DMA periods
    uint32_t codec_id;         // the codec's vendor and device id (/proc/audio)
    const char *detail;        // a short note about the hardware (/proc/audio)
};

/**
 * register the sound device (one, the first one wins) and make /dev/dsp
 * and /dev/mixer.
 * @return false if a device is already registered.
 */
bool audio_register(audio_device_t *dev);

/**
 * fill a DMA period from the ring buffer, with silence after the data.
 * call it from the driver interrupt (or from start).
 * @param dst the period.
 * @param bytes its size (a multiple of 4).
 * @return the bytes of real sound in it.
 */
uint32_t audio_pull(audio_device_t *dev, void *dst, uint32_t bytes);

/**
 * the state for /proc/audio.
 */
typedef struct
{
    bool present;
    const char *name;
    uint32_t codec_id;
    const char *detail;
    uint32_t rate;
    bool open;
    bool running;
    uint32_t queued;    // bytes in the ring
    uint32_t underruns; // periods filled with silence while a program played
    uint64_t played;    // bytes given to the device
    uint32_t volume;    // left | right << 8
} audio_status_t;

void audio_status(audio_status_t *status);

/**
 * the sound output core (see audio.h).
 *
 * each opener of /dev/dsp has a stream: a ring buffer of sound in the
 * device format, 4 bytes per frame (left and right, signed 16-bit little
 * endian). write() converts the program's format into it; audio_pull (the
 * driver interrupt) adds the playing streams together, with clipping. the
 * program's byte counts (GETOSPACE, GETODELAY) are given in its format.
 *
 * a stream plays when its ring holds enough sound for all the device's
 * periods and one more, and stops when the program closes /dev/dsp (after
 * the queued sound has played), on SNDCTL_DSP_SYNC, and on
 * SNDCTL_DSP_RESET. the device runs while a stream plays. all streams use
 * the device's rate: only a program that is alone can change it.
 *
 * locking: one spinlock (with interrupts off) for the streams and the state,
 * shared with the driver interrupt. the driver's start and stop run
 * without it, in process context.
 */

#include "audio.h"

#include "soundcard.h"
#include "arch/arch.h"
#include "common/memory.h"
#include "common/string.h"
#include "kernel/device/device.h"
#include "kernel/errno.h"
#include "kernel/klog/klog.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"
#include "kernel/time/timer.h"
#include "kernel/usermode/uaccess.h"
#include "kernel/vfs/file.h"
#include "kernel/vfs/poll.h"
#include "kernel/vfs/stat.h"
#include "kernel/memory/heap.h"

#define RING_BYTES  32768u // about 170 ms at 48 kHz, for each program
#define FRAME_BYTES 4u     // the device format: 16-bit stereo
#define STREAMS_MAX 8u
#define DRAIN_TIMEOUT_MS 2000u
#define DEFAULT_VOLUME (75u | (75u << 8))

/** the sound of one opener of /dev/dsp */
typedef struct
{
    uint8_t *ring;               // RING_BYTES, in the device format
    uint32_t head;               // the next byte to play
    volatile uint32_t count;     // bytes queued
    uint32_t format;             // AFMT_ of the program
    uint32_t channels;           // 1 or 2
    volatile bool playing;       // in the mix (it had enough sound to start)
    bool draining;               // the end of its sound: short periods are not underruns
    bool starved;                // its last period was short (one underrun per gap)
} stream_t;

static audio_device_t *device;
static spinlock_t lock;

static stream_t *streams[STREAMS_MAX]; // the open ones, NULL for a free place
static uint32_t stream_count;
static int32_t *mix;                   // a period as sums of samples (audio_pull)

static wait_queue_t writers;    // wait for room
static timed_queue_t drainers;  // wait for the sound to play

static volatile bool running;
static volatile uint32_t pulls;  // periods given to the device
static uint32_t underruns;
static uint64_t played;
static uint32_t volume = DEFAULT_VOLUME;

static device_t dsp_device;
static device_t mixer_device;

/**
 * bytes per frame in the program's format.
 */
static uint32_t in_frame_bytes(const stream_t *s)
{
    return s->channels * (s->format == AFMT_U8 ? 1u : 2u);
}

/**
 * device bytes as bytes of the program's format.
 */
static uint32_t to_program_bytes(const stream_t *s, uint32_t bytes)
{
    return bytes / FRAME_BYTES * in_frame_bytes(s);
}

static int16_t read_sample(const stream_t *s, const uint8_t *p)
{
    if (s->format == AFMT_U8)
    {
        return (int16_t)(((int32_t)p[0] - 128) * 256);
    }
    return (int16_t)(p[0] | (p[1] << 8));
}

/**
 * put frames into a stream's ring, converted. call with the lock held.
 */
static void ring_put_frames(stream_t *s, const uint8_t *src, uint32_t frames)
{
    uint32_t sample_bytes = s->format == AFMT_U8 ? 1u : 2u;
    uint32_t tail = (s->head + s->count) % RING_BYTES;

    for (uint32_t i = 0; i < frames; i++)
    {
        int16_t left = read_sample(s, src);
        int16_t right = s->channels == 2 ? read_sample(s, src + sample_bytes) : left;
        src += sample_bytes * s->channels;

        // RING_BYTES is a multiple of 4, so a frame never wraps
        s->ring[tail + 0] = (uint8_t)left;
        s->ring[tail + 1] = (uint8_t)((uint16_t)left >> 8);
        s->ring[tail + 2] = (uint8_t)right;
        s->ring[tail + 3] = (uint8_t)((uint16_t)right >> 8);
        tail = (tail + FRAME_BYTES) % RING_BYTES;
    }
    s->count += frames * FRAME_BYTES;
}

/**
 * bytes to queue before a stream plays: all the device's periods and one
 * more.
 */
static uint32_t start_threshold(void)
{
    uint32_t bytes = (device->periods + 1) * device->period_bytes;
    return bytes < RING_BYTES ? bytes : RING_BYTES;
}

static bool any_playing(void)
{
    for (uint32_t i = 0; i < STREAMS_MAX; i++)
    {
        if (streams[i] && streams[i]->playing)
        {
            return true;
        }
    }
    return false;
}

/**
 * put a stream in the mix, and start the device if it is stopped. call
 * without the lock.
 */
static void play_stream(stream_t *s)
{
    uint32_t flags = spin_lock_irqsave(&lock);
    s->starved = false;
    s->playing = true;
    spin_unlock_irqrestore(&lock, flags);
    if (running)
    {
        return;
    }
    running = device->ops->start(device);
    if (!running)
    {
        KLOG_WARN("audio: %s did not start", device->name);
    }
}

/**
 * take a stream out of the mix; stop the device when no stream plays.
 */
static void unplay_stream(stream_t *s)
{
    uint32_t flags = spin_lock_irqsave(&lock);
    s->playing = false;
    bool stop = running && !any_playing();
    spin_unlock_irqrestore(&lock, flags);
    if (stop)
    {
        device->ops->stop(device);
        running = false;
    }
}

/**
 * wait until a stream's queued sound has played, then take it out.
 */
static void drain_stream(stream_t *s)
{
    // set first: the short periods from now on are the end of the sound,
    // not underruns
    s->draining = true;
    if (s->count > 0 && !s->playing)
    {
        play_stream(s); // less than the start threshold was written
    }

    uint64_t deadline = timer_now() + timer_ms_to_ticks(DRAIN_TIMEOUT_MS);
    uint32_t flags = arch_irq_save();

    // the ring empties first; then each period still in the DMA buffers
    // must play once
    while (running && s->count > 0 && timer_now() < deadline)
    {
        timed_queue_block(&drainers, deadline);
    }
    uint32_t target = pulls + device->periods;
    while (running && s->playing && (int32_t)(target - pulls) > 0 && timer_now() < deadline)
    {
        timed_queue_block(&drainers, deadline);
    }

    s->draining = false;
    arch_irq_restore(flags);
    unplay_stream(s);
}

/**
 * drop a stream's queued sound and take it out of the mix.
 */
static void reset(stream_t *s)
{
    unplay_stream(s);
    uint32_t flags = spin_lock_irqsave(&lock);
    s->head = 0;
    s->count = 0;
    wait_queue_wake_all(&writers);
    spin_unlock_irqrestore(&lock, flags);
}

void audio_mix_add(int32_t *sums, const uint8_t *frames, uint32_t samples)
{
    for (uint32_t i = 0; i < samples; i++)
    {
        sums[i] += (int16_t)(frames[2 * i] | (frames[2 * i + 1] << 8));
    }
}

void audio_mix_out(uint8_t *out, const int32_t *sums, uint32_t samples)
{
    for (uint32_t i = 0; i < samples; i++)
    {
        int32_t v = sums[i] > 32767 ? 32767 : sums[i] < -32768 ? -32768 : sums[i];
        out[2 * i] = (uint8_t)v;
        out[2 * i + 1] = (uint8_t)((uint16_t)v >> 8);
    }
}

/**
 * add up to bytes from a stream to the sums. call with the lock held.
 * @return the bytes taken.
 */
static uint32_t take_from(stream_t *s, uint32_t bytes)
{
    uint32_t take = s->count < bytes ? s->count : bytes;
    uint32_t first = RING_BYTES - s->head < take ? RING_BYTES - s->head : take;
    audio_mix_add(mix, s->ring + s->head, first / 2);
    audio_mix_add(mix + first / 2, s->ring, (take - first) / 2);
    s->head = (s->head + take) % RING_BYTES;
    s->count -= take;
    return take;
}

uint32_t audio_pull(audio_device_t *dev, void *dst, uint32_t bytes)
{
    (void)dev;
    uint8_t *out = dst;
    uint32_t real = 0;
    uint32_t flags = spin_lock_irqsave(&lock);

    memset(mix, 0, bytes / 2 * sizeof(mix[0]));
    for (uint32_t i = 0; i < STREAMS_MAX; i++)
    {
        stream_t *s = streams[i];
        if (!s || !s->playing)
        {
            continue;
        }
        uint32_t take = take_from(s, bytes);
        real = take > real ? take : real;

        // a short period while a program plays is an underrun (counted once
        // per gap, not for each silent period after it)
        if (take < bytes && !s->draining)
        {
            if (!s->starved)
            {
                underruns++;
            }
            s->starved = true;
        }
        else if (take == bytes)
        {
            s->starved = false;
        }
    }
    audio_mix_out(out, mix, bytes / 2);
    played += real;
    pulls++;

    wait_queue_wake_all(&writers);
    spin_unlock_irqrestore(&lock, flags);

    timed_queue_wake_all(&drainers);
    poll_notify();
    return real;
}

static int32_t dsp_write(file_t *file, const void *buf, uint32_t count)
{
    stream_t *s = file->device_data;
    uint32_t in_frame = in_frame_bytes(s);
    uint32_t frames_total = count / in_frame;
    uint32_t frames_done = 0;
    const uint8_t *src = buf;
    int32_t result = 0;

    while (frames_done < frames_total)
    {
        uint32_t flags = spin_lock_irqsave(&lock);
        uint32_t room = (RING_BYTES - s->count) / FRAME_BYTES;
        if (room == 0)
        {
            spin_unlock_irqrestore(&lock, flags);
            if (!s->playing)
            {
                play_stream(s); // the ring is full: play it
                if (!running)
                {
                    result = -EIO;
                    break;
                }
                continue;
            }
            if (file->flags & O_NONBLOCK)
            {
                result = -EAGAIN;
                break;
            }
            if (signal_pending(process_current()))
            {
                result = -ERESTARTSYS;
                break;
            }
            flags = spin_lock_irqsave(&lock);
            if (s->count + FRAME_BYTES > RING_BYTES)
            {
                wait_queue_block_locked_interruptible(&writers, &lock);
            }
            spin_unlock_irqrestore(&lock, flags);
            continue;
        }

        uint32_t frames = frames_total - frames_done < room ? frames_total - frames_done : room;
        ring_put_frames(s, src + frames_done * in_frame, frames);
        frames_done += frames;
        bool start = !s->playing && s->count >= start_threshold();
        spin_unlock_irqrestore(&lock, flags);

        if (start)
        {
            play_stream(s);
        }
    }

    // the frames that went in count, even if the rest could not
    return frames_done > 0 ? (int32_t)(frames_done * in_frame) : result;
}

static uint32_t dsp_poll(file_t *file)
{
    stream_t *s = file->device_data;
    return RING_BYTES - s->count >= device->period_bytes ? POLLOUT : 0;
}

static int32_t put_int(void *arg, int32_t value)
{
    return kerr_to_errno(copy_to_user(arg, &value, sizeof(value)));
}

static int32_t get_int(void *arg, int32_t *value)
{
    return kerr_to_errno(copy_from_user(value, arg, sizeof(*value)));
}

/**
 * the rate is the device's, for all the programs: one program alone can
 * change it; with others open it stays (OSS tells the rate in use).
 */
static uint32_t set_rate(stream_t *s, uint32_t rate)
{
    if (stream_count > 1)
    {
        return device->rate;
    }
    reset(s);
    device->rate = device->ops->set_rate(device, rate);
    return device->rate;
}

static int32_t dsp_ioctl(file_t *file, uint32_t request, void *arg)
{
    stream_t *s = file->device_data;
    int32_t value = 0;
    int32_t err;

    switch (request)
    {
    case SNDCTL_DSP_RESET:
        reset(s);
        return 0;

    case SNDCTL_DSP_SYNC:
        drain_stream(s);
        return 0;

    case SNDCTL_DSP_SPEED:
        if ((err = get_int(arg, &value)) < 0)
            return err;
        if (value <= 0)
            return -EINVAL;
        return put_int(arg, (int32_t)set_rate(s, (uint32_t)value));

    case SNDCTL_DSP_STEREO:
        if ((err = get_int(arg, &value)) < 0)
            return err;
        reset(s);
        s->channels = value ? 2 : 1;
        return put_int(arg, s->channels == 2);

    case SNDCTL_DSP_CHANNELS:
        if ((err = get_int(arg, &value)) < 0)
            return err;
        reset(s);
        s->channels = value == 1 ? 1 : 2;
        return put_int(arg, (int32_t)s->channels);

    case SNDCTL_DSP_SETFMT:
        if ((err = get_int(arg, &value)) < 0)
            return err;
        if (value == AFMT_U8 || value == AFMT_S16_LE)
        {
            reset(s);
            s->format = (uint32_t)value;
        }
        return put_int(arg, (int32_t)s->format);

    case SNDCTL_DSP_SETFRAGMENT:
        // a request only: the device keeps the period it plays in, which
        // SNDCTL_DSP_GETOSPACE and SNDCTL_DSP_GETBLKSIZE give
        return 0;
    case SNDCTL_DSP_GETFMTS:
        return put_int(arg, AFMT_U8 | AFMT_S16_LE);

    case SNDCTL_DSP_GETBLKSIZE:
        return put_int(arg, (int32_t)to_program_bytes(s, device->period_bytes));

    case SNDCTL_DSP_GETOSPACE:
    {
        audio_buf_info info;
        uint32_t fragment = to_program_bytes(s, device->period_bytes);
        info.fragsize = (int32_t)fragment;
        info.fragstotal = (int32_t)(RING_BYTES / device->period_bytes);
        info.bytes = (int32_t)to_program_bytes(s, RING_BYTES - s->count);
        info.fragments = info.bytes / info.fragsize;
        return kerr_to_errno(copy_to_user(arg, &info, sizeof(info)));
    }

    case SNDCTL_DSP_GETODELAY:
    {
        // the ring, and the periods in the DMA buffers while playing
        uint32_t queued = s->count;
        if (s->playing)
        {
            queued += device->periods * device->period_bytes;
        }
        return put_int(arg, (int32_t)to_program_bytes(s, queued));
    }

    default:
        return -ENOTTY;
    }
}

static int32_t dsp_stat(file_t *file, struct kstat *st)
{
    (void)file;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0666;
    st->st_nlink = 1;
    st->st_blksize = device->period_bytes;
    return 0;
}

static void dsp_release(file_t *file)
{
    stream_t *s = file->device_data;
    drain_stream(s);
    reset(s);
    uint32_t flags = spin_lock_irqsave(&lock);
    for (uint32_t i = 0; i < STREAMS_MAX; i++)
    {
        if (streams[i] == s)
        {
            streams[i] = NULL;
        }
    }
    stream_count--;
    spin_unlock_irqrestore(&lock, flags);
    kfree(s->ring);
    kfree(s);
    kfree(file);
}

static const file_ops_t dsp_file_ops = {
    .write = dsp_write,
    .ioctl = dsp_ioctl,
    .stat = dsp_stat,
    .poll = dsp_poll,
    .release = dsp_release,
};

static kerr_t dsp_open(device_t *dev, uint32_t flags, file_t **out)
{
    (void)dev;
    if ((flags & O_ACCMODE) == O_RDONLY)
    {
        return KERR_INVAL; // no recording
    }

    stream_t *s = kmalloc(sizeof(*s));
    uint8_t *ring = kmalloc(RING_BYTES);
    file_t *file = s && ring ? file_new(&dsp_file_ops, flags) : NULL;
    if (!file)
    {
        kfree(ring);
        kfree(s);
        return KERR_NOMEM;
    }
    memset(s, 0, sizeof(*s));
    s->ring = ring;
    // the OSS defaults: unsigned 8-bit mono (at 8 kHz, below)
    s->format = AFMT_U8;
    s->channels = 1;

    uint32_t irq_flags = spin_lock_irqsave(&lock);
    uint32_t place = STREAMS_MAX;
    for (uint32_t i = 0; i < STREAMS_MAX && place == STREAMS_MAX; i++)
    {
        if (!streams[i])
        {
            place = i;
        }
    }
    if (place < STREAMS_MAX)
    {
        streams[place] = s;
        stream_count++;
    }
    spin_unlock_irqrestore(&lock, irq_flags);
    if (place == STREAMS_MAX)
    {
        kfree(ring);
        kfree(s);
        kfree(file);
        return KERR_BUSY; // STREAMS_MAX programs play already
    }

    file->device_data = s;
    if (stream_count == 1)
    {
        device->rate = device->ops->set_rate(device, 8000);
    }
    *out = file;
    return KERR_OK;
}

static const device_ops_t dsp_ops = {
    .open = dsp_open,
};

static void apply_volume(void)
{
    if (device->ops->set_volume)
    {
        device->ops->set_volume(device, volume & 0xFF, (volume >> 8) & 0xFF);
    }
}

static int32_t mixer_ioctl(device_t *dev, uint32_t request, void *arg)
{
    (void)dev;
    int32_t value;
    int32_t err;

    switch (request)
    {
    case SOUND_MIXER_READ_VOLUME:
        return put_int(arg, (int32_t)volume);

    case SOUND_MIXER_WRITE_VOLUME:
    {
        if ((err = get_int(arg, &value)) < 0)
            return err;
        uint32_t left = (uint32_t)value & 0xFF;
        uint32_t right = ((uint32_t)value >> 8) & 0xFF;
        left = left > 100 ? 100 : left;
        right = right > 100 ? 100 : right;
        volume = left | (right << 8);
        apply_volume();
        return put_int(arg, (int32_t)volume);
    }

    case SOUND_MIXER_READ_DEVMASK:
        return put_int(arg, SOUND_MASK_VOLUME);

    default:
        return -ENOTTY;
    }
}

static const device_ops_t mixer_ops = {
    .ioctl = mixer_ioctl,
};

static void add_device(device_t *dev, const char *name, const device_ops_t *ops)
{
    strncpy(dev->name, name, DEVICE_NAME_MAX - 1);
    dev->name[DEVICE_NAME_MAX - 1] = '\0';
    dev->type = DEVICE_TYPE_CHAR;
    dev->ops = ops;
    dev->driver_data = NULL;
    device_register(dev);
}

bool audio_register(audio_device_t *dev)
{
    if (device || !dev || !dev->ops || !dev->ops->start || !dev->ops->stop ||
        !dev->ops->set_rate || dev->period_bytes == 0 || dev->period_bytes % FRAME_BYTES != 0 ||
        dev->periods == 0 || dev->periods * dev->period_bytes >= RING_BYTES)
    {
        return false;
    }
    mix = kmalloc(dev->period_bytes / 2 * sizeof(int32_t));
    if (!mix)
    {
        return false;
    }

    spinlock_init(&lock);
    wait_queue_init(&writers);
    timed_queue_init(&drainers);
    device = dev;
    apply_volume();

    add_device(&dsp_device, "dsp", &dsp_ops);
    add_device(&mixer_device, "mixer", &mixer_ops);
    KLOG_INFO("audio: %s, %u Hz, %u periods of %u bytes", dev->name, dev->rate, dev->periods,
              dev->period_bytes);
    return true;
}

void audio_status(audio_status_t *status)
{
    memset(status, 0, sizeof(*status));
    if (!device)
    {
        return;
    }
    status->present = true;
    status->name = device->name;
    status->codec_id = device->codec_id;
    status->detail = device->detail;
    status->rate = device->rate;
    status->open = stream_count;
    status->running = running;
    uint32_t flags = spin_lock_irqsave(&lock);
    for (uint32_t i = 0; i < STREAMS_MAX; i++)
    {
        status->queued += streams[i] ? streams[i]->count : 0;
    }
    spin_unlock_irqrestore(&lock, flags);
    status->underruns = underruns;
    status->played = played;
    status->volume = volume;
}

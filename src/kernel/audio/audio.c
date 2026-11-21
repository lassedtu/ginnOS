/**
 * the sound output core (see audio.h).
 *
 * the ring buffer holds sound in the device format: 4 bytes per frame
 * (left and right, signed 16-bit little endian). write() converts the
 * program's format into it; audio_pull (the driver interrupt) takes it
 * out. the program's byte counts (GETOSPACE, GETODELAY) are given in the
 * program's format.
 *
 * the device starts when the ring holds enough sound for all its periods
 * and one more, and stops when the program closes /dev/dsp (after the
 * queued sound has played), on SNDCTL_DSP_SYNC, and on SNDCTL_DSP_RESET.
 *
 * locking: one spinlock (with interrupts off) for the ring and the state,
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

#define RING_BYTES  32768u // about 170 ms at 48 kHz
#define FRAME_BYTES 4u     // the device format: 16-bit stereo
#define DRAIN_TIMEOUT_MS 2000u
#define DEFAULT_VOLUME (75u | (75u << 8))

static audio_device_t *device;
static spinlock_t lock;

static uint8_t *ring; // RING_BYTES, from the heap when a device registers
static uint32_t ring_head;           // the next byte to play
static volatile uint32_t ring_count; // bytes queued

static wait_queue_t writers;    // wait for room
static timed_queue_t drainers;  // wait for the sound to play

static bool is_open;
static volatile bool running;
static bool draining;
static bool starved;             // the last period was short (one underrun per gap)
static volatile uint32_t pulls;  // periods given to the device
static uint32_t format;          // AFMT_ of the program
static uint32_t channels;        // 1 or 2
static uint32_t underruns;
static uint64_t played;
static uint32_t volume = DEFAULT_VOLUME;

static device_t dsp_device;
static device_t mixer_device;

/* ---- formats ------------------------------------------------------------- */

/**
 * bytes per frame in the program's format.
 */
static uint32_t in_frame_bytes(void)
{
    return channels * (format == AFMT_U8 ? 1u : 2u);
}

/**
 * device bytes as bytes of the program's format.
 */
static uint32_t to_program_bytes(uint32_t bytes)
{
    return bytes / FRAME_BYTES * in_frame_bytes();
}

static int16_t read_sample(const uint8_t *p)
{
    if (format == AFMT_U8)
    {
        return (int16_t)(((int32_t)p[0] - 128) * 256);
    }
    return (int16_t)(p[0] | (p[1] << 8));
}

/**
 * put frames into the ring, converted. call with the lock held.
 */
static void ring_put_frames(const uint8_t *src, uint32_t frames)
{
    uint32_t sample_bytes = format == AFMT_U8 ? 1u : 2u;
    uint32_t tail = (ring_head + ring_count) % RING_BYTES;

    for (uint32_t i = 0; i < frames; i++)
    {
        int16_t left = read_sample(src);
        int16_t right = channels == 2 ? read_sample(src + sample_bytes) : left;
        src += sample_bytes * channels;

        // RING_BYTES is a multiple of 4, so a frame never wraps
        ring[tail + 0] = (uint8_t)left;
        ring[tail + 1] = (uint8_t)((uint16_t)left >> 8);
        ring[tail + 2] = (uint8_t)right;
        ring[tail + 3] = (uint8_t)((uint16_t)right >> 8);
        tail = (tail + FRAME_BYTES) % RING_BYTES;
    }
    ring_count += frames * FRAME_BYTES;
}

/* ---- the device ---------------------------------------------------------- */

/**
 * bytes to queue before the device starts: all its periods and one more.
 */
static uint32_t start_threshold(void)
{
    uint32_t bytes = (device->periods + 1) * device->period_bytes;
    return bytes < RING_BYTES ? bytes : RING_BYTES;
}

static void start_device(void)
{
    if (running)
    {
        return;
    }
    starved = false;
    running = device->ops->start(device);
    if (!running)
    {
        KLOG_WARN("audio: %s did not start", device->name);
    }
}

static void stop_device(void)
{
    if (!running)
    {
        return;
    }
    device->ops->stop(device);
    running = false;
}

/**
 * wait until the queued sound has played, then stop the device.
 */
static void drain_and_stop(void)
{
    // set first: the short periods from now on are the end of the sound,
    // not underruns
    draining = true;
    if (ring_count > 0)
    {
        start_device(); // less than the start threshold was written
    }

    uint64_t deadline = timer_now() + timer_ms_to_ticks(DRAIN_TIMEOUT_MS);
    uint32_t flags = arch_irq_save();

    // the ring empties first; then each period still in the DMA buffers
    // must play once
    while (running && ring_count > 0 && timer_now() < deadline)
    {
        timed_queue_block(&drainers, deadline);
    }
    uint32_t target = pulls + device->periods;
    while (running && (int32_t)(target - pulls) > 0 && timer_now() < deadline)
    {
        timed_queue_block(&drainers, deadline);
    }

    draining = false;
    arch_irq_restore(flags);
    stop_device();
}

/**
 * drop the queued sound and stop.
 */
static void reset(void)
{
    stop_device();
    uint32_t flags = spin_lock_irqsave(&lock);
    ring_head = 0;
    ring_count = 0;
    wait_queue_wake_all(&writers);
    spin_unlock_irqrestore(&lock, flags);
}

uint32_t audio_pull(audio_device_t *dev, void *dst, uint32_t bytes)
{
    (void)dev;
    uint8_t *out = dst;
    uint32_t flags = spin_lock_irqsave(&lock);

    uint32_t take = ring_count < bytes ? ring_count : bytes;
    uint32_t first = RING_BYTES - ring_head < take ? RING_BYTES - ring_head : take;
    memcpy(out, ring + ring_head, first);
    memcpy(out + first, ring, take - first);
    memset(out + take, 0, bytes - take);
    ring_head = (ring_head + take) % RING_BYTES;
    ring_count -= take;
    played += take;
    pulls++;

    // a short period while a program plays is an underrun (counted once
    // per gap, not for each silent period after it)
    if (take < bytes && is_open && !draining)
    {
        if (!starved)
        {
            underruns++;
        }
        starved = true;
    }
    else if (take == bytes)
    {
        starved = false;
    }

    wait_queue_wake_all(&writers);
    spin_unlock_irqrestore(&lock, flags);

    timed_queue_wake_all(&drainers);
    poll_notify();
    return take;
}

/* ---- /dev/dsp ------------------------------------------------------------ */

static int32_t dsp_write(file_t *file, const void *buf, uint32_t count)
{
    uint32_t in_frame = in_frame_bytes();
    uint32_t frames_total = count / in_frame;
    uint32_t frames_done = 0;
    const uint8_t *src = buf;
    int32_t result = 0;

    while (frames_done < frames_total)
    {
        uint32_t flags = spin_lock_irqsave(&lock);
        uint32_t room = (RING_BYTES - ring_count) / FRAME_BYTES;
        if (room == 0)
        {
            spin_unlock_irqrestore(&lock, flags);
            if (!running)
            {
                start_device(); // the ring is full: play it
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
            if (ring_count + FRAME_BYTES > RING_BYTES)
            {
                wait_queue_block_locked_interruptible(&writers, &lock);
            }
            spin_unlock_irqrestore(&lock, flags);
            continue;
        }

        uint32_t frames = frames_total - frames_done < room ? frames_total - frames_done : room;
        ring_put_frames(src + frames_done * in_frame, frames);
        frames_done += frames;
        bool start = !running && ring_count >= start_threshold();
        spin_unlock_irqrestore(&lock, flags);

        if (start)
        {
            start_device();
        }
    }

    // the frames that went in count, even if the rest could not
    return frames_done > 0 ? (int32_t)(frames_done * in_frame) : result;
}

static uint32_t dsp_poll(file_t *file)
{
    (void)file;
    return RING_BYTES - ring_count >= device->period_bytes ? POLLOUT : 0;
}

static int32_t put_int(void *arg, int32_t value)
{
    return kerr_to_errno(copy_to_user(arg, &value, sizeof(value)));
}

static int32_t get_int(void *arg, int32_t *value)
{
    return kerr_to_errno(copy_from_user(value, arg, sizeof(*value)));
}

static int32_t dsp_ioctl(file_t *file, uint32_t request, void *arg)
{
    (void)file;
    int32_t value = 0;
    int32_t err;

    switch (request)
    {
    case SNDCTL_DSP_RESET:
        reset();
        return 0;

    case SNDCTL_DSP_SYNC:
        drain_and_stop();
        return 0;

    case SNDCTL_DSP_SPEED:
        if ((err = get_int(arg, &value)) < 0)
            return err;
        if (value <= 0)
            return -EINVAL;
        reset();
        device->rate = device->ops->set_rate(device, (uint32_t)value);
        return put_int(arg, (int32_t)device->rate);

    case SNDCTL_DSP_STEREO:
        if ((err = get_int(arg, &value)) < 0)
            return err;
        reset();
        channels = value ? 2 : 1;
        return put_int(arg, channels == 2);

    case SNDCTL_DSP_CHANNELS:
        if ((err = get_int(arg, &value)) < 0)
            return err;
        reset();
        channels = value == 1 ? 1 : 2;
        return put_int(arg, (int32_t)channels);

    case SNDCTL_DSP_SETFMT:
        if ((err = get_int(arg, &value)) < 0)
            return err;
        if (value == AFMT_U8 || value == AFMT_S16_LE)
        {
            reset();
            format = (uint32_t)value;
        }
        return put_int(arg, (int32_t)format);

    case SNDCTL_DSP_GETFMTS:
        return put_int(arg, AFMT_U8 | AFMT_S16_LE);

    case SNDCTL_DSP_GETBLKSIZE:
        return put_int(arg, (int32_t)to_program_bytes(device->period_bytes));

    case SNDCTL_DSP_GETOSPACE:
    {
        audio_buf_info info;
        uint32_t fragment = to_program_bytes(device->period_bytes);
        info.fragsize = (int32_t)fragment;
        info.fragstotal = (int32_t)(RING_BYTES / device->period_bytes);
        info.bytes = (int32_t)to_program_bytes(RING_BYTES - ring_count);
        info.fragments = info.bytes / info.fragsize;
        return kerr_to_errno(copy_to_user(arg, &info, sizeof(info)));
    }

    case SNDCTL_DSP_GETODELAY:
    {
        // the ring, and the periods in the DMA buffers while playing
        uint32_t queued = ring_count;
        if (running)
        {
            queued += device->periods * device->period_bytes;
        }
        return put_int(arg, (int32_t)to_program_bytes(queued));
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
    drain_and_stop();
    reset();
    uint32_t flags = spin_lock_irqsave(&lock);
    is_open = false;
    spin_unlock_irqrestore(&lock, flags);
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

    uint32_t irq_flags = spin_lock_irqsave(&lock);
    if (is_open)
    {
        spin_unlock_irqrestore(&lock, irq_flags);
        return KERR_BUSY;
    }
    is_open = true;
    spin_unlock_irqrestore(&lock, irq_flags);

    file_t *file = file_new(&dsp_file_ops, flags);
    if (!file)
    {
        is_open = false;
        return KERR_NOMEM;
    }
    file->device_data = device;

    // the OSS defaults: unsigned 8-bit mono at 8 kHz
    format = AFMT_U8;
    channels = 1;
    device->rate = device->ops->set_rate(device, 8000);
    *out = file;
    return KERR_OK;
}

static const device_ops_t dsp_ops = {
    .open = dsp_open,
};

/* ---- /dev/mixer ---------------------------------------------------------- */

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

/* ---- registration -------------------------------------------------------- */

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
    ring = kmalloc(RING_BYTES);
    if (!ring)
    {
        return false;
    }

    spinlock_init(&lock);
    wait_queue_init(&writers);
    timed_queue_init(&drainers);
    format = AFMT_U8;
    channels = 1;
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
    status->open = is_open;
    status->running = running;
    status->queued = ring_count;
    status->underruns = underruns;
    status->played = played;
    status->volume = volume;
}

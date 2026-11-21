/**
 * @file audiotest.c
 * @brief test the sound output (/dev/dsp, /dev/mixer).
 *
 * usage: audiotest tone [hz] [ms]   a sine tone (default 440 Hz, 1000 ms)
 *        audiotest left | right      a tone on one side only
 *        audiotest sweep             100 Hz to 4 kHz in 2 seconds
 *        audiotest api               the ioctls, EBUSY, poll, O_NONBLOCK
 *        audiotest latency           how long a short click takes to play
 *
 * the tones play at 48 kHz, 16-bit stereo, or at the rate the device
 * gives back.
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/soundcard.h>
#include <time.h>
#include <unistd.h>

#define PI 3.14159265358979323846
#define CHUNK_FRAMES 1024
#define AMPLITUDE 12000.0

static int failed(const char *what)
{
    printf("audiotest: %s (errno %d)\n", what, errno);
    return 1;
}

/**
 * open /dev/dsp for 16-bit stereo at a rate.
 * @return the fd, or -1. *rate gets the rate in use.
 */
static int open_dsp(int flags, int *rate)
{
    int fd = open("/dev/dsp", O_WRONLY | flags);
    if (fd < 0)
        return -1;

    int format = AFMT_S16_LE;
    int channels = 2;
    if (ioctl(fd, SNDCTL_DSP_SETFMT, &format) < 0 || format != AFMT_S16_LE ||
        ioctl(fd, SNDCTL_DSP_CHANNELS, &channels) < 0 || channels != 2 ||
        ioctl(fd, SNDCTL_DSP_SPEED, rate) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    while (len > 0)
    {
        ssize_t n = write(fd, p, len);
        if (n <= 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/**
 * play a tone whose frequency goes from hz_start to hz_end.
 * @param left_gain the left volume, 0 or 1 (likewise right_gain).
 */
static int play_tone(double hz_start, double hz_end, int ms, double left_gain, double right_gain)
{
    int rate = 48000;
    int fd = open_dsp(0, &rate);
    if (fd < 0)
        return failed("cannot open /dev/dsp");

    int16_t buf[CHUNK_FRAMES * 2];
    long total = (long)rate * ms / 1000;
    double phase = 0;
    for (long done = 0; done < total;)
    {
        int frames = total - done < CHUNK_FRAMES ? (int)(total - done) : CHUNK_FRAMES;
        for (int i = 0; i < frames; i++)
        {
            double hz = hz_start + (hz_end - hz_start) * (double)(done + i) / (double)total;
            double sample = sin(phase) * AMPLITUDE;
            phase += 2 * PI * hz / rate;
            if (phase > 2 * PI)
                phase -= 2 * PI;
            buf[i * 2] = (int16_t)(sample * left_gain);
            buf[i * 2 + 1] = (int16_t)(sample * right_gain);
        }
        if (write_all(fd, buf, (size_t)frames * 4) < 0)
        {
            close(fd);
            return failed("write");
        }
        done += frames;
    }

    // close waits until the sound has played
    close(fd);
    printf("audiotest: %d ms at %d Hz, ok\n", ms, rate);
    return 0;
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/**
 * the interface: the ioctls answer, a second open is EBUSY, a full
 * buffer gives EAGAIN with O_NONBLOCK and poll waits for room.
 */
static int test_api(void)
{
    int rate = 44100;
    int fd = open_dsp(O_NONBLOCK, &rate);
    if (fd < 0)
        return failed("open /dev/dsp");

    int formats = 0;
    if (ioctl(fd, SNDCTL_DSP_GETFMTS, &formats) < 0 || !(formats & AFMT_S16_LE))
        return failed("SNDCTL_DSP_GETFMTS");

    if (open("/dev/dsp", O_WRONLY) >= 0 || errno != EBUSY)
        return failed("a second open is not EBUSY");

    audio_buf_info info;
    if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &info) < 0 || info.bytes <= 0 || info.fragsize <= 0)
        return failed("SNDCTL_DSP_GETOSPACE");
    int empty_space = info.bytes;

    // fill the buffer without blocking until it is full
    static int16_t silence[CHUNK_FRAMES * 2];
    long written = 0;
    for (;;)
    {
        ssize_t n = write(fd, silence, sizeof(silence));
        if (n < 0)
        {
            if (errno != EAGAIN)
                return failed("non-blocking write");
            break;
        }
        written += n;
        if (written > 10L * 1024 * 1024)
            return failed("the buffer never fills");
    }
    if (written < empty_space)
        return failed("fewer bytes fit than GETOSPACE said");

    int delay = 0;
    if (ioctl(fd, SNDCTL_DSP_GETODELAY, &delay) < 0 || delay <= 0)
        return failed("SNDCTL_DSP_GETODELAY");

    // the device plays, so room comes back
    struct pollfd pfd = {.fd = fd, .events = POLLOUT, .revents = 0};
    long start = now_ms();
    if (poll(&pfd, 1, 2000) != 1 || !(pfd.revents & POLLOUT))
        return failed("poll for room");
    long waited = now_ms() - start;

    if (ioctl(fd, SNDCTL_DSP_RESET, NULL) < 0)
        return failed("SNDCTL_DSP_RESET");
    if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &info) < 0 || info.bytes != empty_space)
        return failed("the buffer is not empty after SNDCTL_DSP_RESET");

    int volume = 0;
    int mixer = open("/dev/mixer", O_RDWR);
    if (mixer < 0 || ioctl(mixer, SOUND_MIXER_READ_VOLUME, &volume) < 0)
        return failed("/dev/mixer");
    int set = 50 | (60 << 8);
    int back = 0;
    if (ioctl(mixer, SOUND_MIXER_WRITE_VOLUME, &set) < 0 ||
        ioctl(mixer, SOUND_MIXER_READ_VOLUME, &back) < 0 || back != (50 | (60 << 8)))
        return failed("SOUND_MIXER_WRITE_VOLUME");
    ioctl(mixer, SOUND_MIXER_WRITE_VOLUME, &volume);
    close(mixer);
    close(fd);

    printf("audiotest api: %d Hz, %d bytes buffer, room after %ld ms, ok\n", rate, empty_space,
           waited);
    return 0;
}

/**
 * write a short click into an empty device and measure until close
 * returns (the click has played): about the start threshold and the DMA
 * periods.
 */
static int test_latency(void)
{
    int rate = 48000;
    int fd = open_dsp(0, &rate);
    if (fd < 0)
        return failed("open /dev/dsp");

    int16_t click[64 * 2];
    for (int i = 0; i < 64; i++)
    {
        click[i * 2] = (int16_t)(i < 32 ? AMPLITUDE : -AMPLITUDE);
        click[i * 2 + 1] = click[i * 2];
    }
    long start = now_ms();
    if (write_all(fd, click, sizeof(click)) < 0 || ioctl(fd, SNDCTL_DSP_SYNC, NULL) < 0)
        return failed("write and sync");
    long took = now_ms() - start;
    close(fd);
    printf("audiotest latency: %ld ms from write to played\n", took);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";

    if (strcmp(mode, "tone") == 0)
    {
        double hz = argc > 2 ? atof(argv[2]) : 440.0;
        int ms = argc > 3 ? atoi(argv[3]) : 1000;
        return play_tone(hz, hz, ms, 1.0, 1.0);
    }
    if (strcmp(mode, "left") == 0)
        return play_tone(440, 440, 1000, 1.0, 0.0);
    if (strcmp(mode, "right") == 0)
        return play_tone(660, 660, 1000, 0.0, 1.0);
    if (strcmp(mode, "sweep") == 0)
        return play_tone(100, 4000, 2000, 1.0, 1.0);
    if (strcmp(mode, "api") == 0)
        return test_api();
    if (strcmp(mode, "latency") == 0)
        return test_latency();

    printf("usage: audiotest tone [hz] [ms]|left|right|sweep|api|latency\n");
    return 2;
}

/**
 * @file play.c
 * @brief play WAV files on /dev/dsp.
 *
 * usage: play file.wav ...
 *
 * takes PCM WAV files: 8-bit or 16-bit, mono or stereo, any sample rate.
 * the sound goes to the device as 16-bit stereo; when the device does not
 * have the file's rate, play converts it (linear interpolation between
 * two samples).
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/soundcard.h>
#include <unistd.h>

#define OUT_FRAMES 1024

typedef struct
{
    uint32_t rate;
    uint16_t channels;
    uint16_t bits;
    uint32_t data_offset;
    uint32_t data_bytes;
} wav_t;

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/**
 * read the RIFF chunks: the format, and where the data is.
 * @return 0, or an error message.
 */
static const char *read_header(int fd, wav_t *wav)
{
    uint8_t head[12];
    if (read(fd, head, 12) != 12 || memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WAVE", 4) != 0)
        return "not a WAV file";

    bool have_format = false;
    uint32_t offset = 12;
    for (;;)
    {
        uint8_t chunk[8];
        if (read(fd, chunk, 8) != 8)
            return "no data chunk";
        uint32_t size = le32(chunk + 4);
        offset += 8;

        if (memcmp(chunk, "fmt ", 4) == 0)
        {
            uint8_t fmt[16];
            if (size < 16 || read(fd, fmt, 16) != 16)
                return "bad format chunk";
            if (le16(fmt) != 1)
                return "not PCM (compressed WAV files are not supported)";
            wav->channels = le16(fmt + 2);
            wav->rate = le32(fmt + 4);
            wav->bits = le16(fmt + 14);
            if ((wav->channels != 1 && wav->channels != 2) || (wav->bits != 8 && wav->bits != 16) ||
                wav->rate == 0)
                return "only 8-bit or 16-bit, mono or stereo";
            have_format = true;
            size -= 16;
            offset += 16;
        }
        else if (memcmp(chunk, "data", 4) == 0)
        {
            if (!have_format)
                return "data before the format";
            wav->data_offset = offset;
            wav->data_bytes = size;
            return 0;
        }

        // skip the rest of the chunk (chunks are padded to even sizes)
        uint32_t skip = size + (size & 1);
        if (lseek(fd, (off_t)(offset + skip), SEEK_SET) < 0)
            return "cannot seek";
        offset += skip;
    }
}

/**
 * reads the data chunk in blocks.
 */
typedef struct
{
    int fd;
    const wav_t *wav;
    uint8_t buf[4096];
    uint32_t used;
    uint32_t filled;
    uint32_t left_bytes; // data bytes not yet read from the file
} reader_t;

/**
 * read the next frame of the file as 16-bit left and right.
 * @return false at the end of the data.
 */
static bool next_frame(reader_t *r, int16_t *left, int16_t *right)
{
    uint32_t frame = r->wav->channels * (r->wav->bits / 8);
    if (r->filled - r->used < frame)
    {
        uint32_t keep = r->filled - r->used;
        memmove(r->buf, r->buf + r->used, keep);
        uint32_t want = sizeof(r->buf) - keep;
        if (want > r->left_bytes)
            want = r->left_bytes;
        ssize_t n = want > 0 ? read(r->fd, r->buf + keep, want) : 0;
        if (n < 0)
            n = 0;
        r->left_bytes -= (uint32_t)n;
        r->used = 0;
        r->filled = keep + (uint32_t)n;
        if (r->filled < frame)
            return false;
    }

    const uint8_t *p = r->buf + r->used;
    r->used += frame;
    if (r->wav->bits == 8)
    {
        *left = (int16_t)((p[0] - 128) * 256);
        *right = r->wav->channels == 2 ? (int16_t)((p[1] - 128) * 256) : *left;
    }
    else
    {
        *left = (int16_t)le16(p);
        *right = r->wav->channels == 2 ? (int16_t)le16(p + 2) : *left;
    }
    return true;
}

static int play_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        printf("play: %s: cannot open (errno %d)\n", path, errno);
        return 1;
    }
    wav_t wav;
    const char *error = read_header(fd, &wav);
    if (error)
    {
        printf("play: %s: %s\n", path, error);
        close(fd);
        return 1;
    }

    int dsp = open("/dev/dsp", O_WRONLY);
    if (dsp < 0)
    {
        printf("play: /dev/dsp: %s\n", errno == EBUSY ? "in use" : "no sound device");
        close(fd);
        return 1;
    }
    int format = AFMT_S16_LE;
    int channels = 2;
    int rate = (int)wav.rate;
    ioctl(dsp, SNDCTL_DSP_SETFMT, &format);
    ioctl(dsp, SNDCTL_DSP_CHANNELS, &channels);
    ioctl(dsp, SNDCTL_DSP_SPEED, &rate);

    uint32_t tenths = (uint32_t)((uint64_t)wav.data_bytes * 10 /
                                 (wav.rate * wav.channels * (wav.bits / 8)));
    printf("play: %s: %u Hz, %u-bit, %s, %u.%u s", path, wav.rate, wav.bits,
           wav.channels == 2 ? "stereo" : "mono", tenths / 10, tenths % 10);
    if ((uint32_t)rate != wav.rate)
        printf(" (converted to %d Hz)", rate);
    printf("\n");

    reader_t reader = {.fd = fd, .wav = &wav, .used = 0, .filled = 0,
                       .left_bytes = wav.data_bytes};

    // step through the file in fixed point (16 fraction bits): each
    // output frame advances the file position by file rate / device rate
    uint32_t step = (uint32_t)(((uint64_t)wav.rate << 16) / (uint32_t)rate);
    uint32_t position = 0; // the fraction between frame a and frame b
    int16_t a_left = 0, a_right = 0, b_left = 0, b_right = 0;
    bool more = next_frame(&reader, &a_left, &a_right) && next_frame(&reader, &b_left, &b_right);

    int16_t out[OUT_FRAMES * 2];
    while (more)
    {
        int n = 0;
        while (n < OUT_FRAMES && more)
        {
            int32_t f = (int32_t)position;
            out[n * 2] = (int16_t)(a_left + (((b_left - a_left) * f) >> 16));
            out[n * 2 + 1] = (int16_t)(a_right + (((b_right - a_right) * f) >> 16));
            n++;

            position += step;
            while (position >= 0x10000u && more)
            {
                position -= 0x10000u;
                a_left = b_left;
                a_right = b_right;
                more = next_frame(&reader, &b_left, &b_right);
            }
        }

        const char *p = (const char *)out;
        size_t len = (size_t)n * 4;
        while (len > 0)
        {
            ssize_t w = write(dsp, p, len);
            if (w <= 0)
            {
                printf("play: write failed (errno %d)\n", errno);
                close(dsp);
                close(fd);
                return 1;
            }
            p += w;
            len -= (size_t)w;
        }
    }

    close(dsp); // waits until the sound has played
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: play file.wav ...\n");
        return 2;
    }
    int status = 0;
    for (int i = 1; i < argc; i++)
        status |= play_file(argv[i]);
    return status;
}

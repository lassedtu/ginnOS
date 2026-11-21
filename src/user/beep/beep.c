/**
 * @file beep.c
 * @brief play a short tone.
 *
 * usage: beep [hz] [ms]   (default 880 Hz, 150 ms)
 *
 * the tone fades in and out over 5 ms, so it starts and stops without a
 * click.
 */

#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/soundcard.h>
#include <unistd.h>

#define PI 3.14159265358979323846
#define AMPLITUDE 10000.0
#define FADE_MS 5

int main(int argc, char **argv)
{
    double hz = argc > 1 ? atof(argv[1]) : 880.0;
    int ms = argc > 2 ? atoi(argv[2]) : 150;
    if (hz <= 0 || ms <= 0)
    {
        printf("usage: beep [hz] [ms]\n");
        return 2;
    }

    int fd = open("/dev/dsp", O_WRONLY);
    if (fd < 0)
    {
        printf("beep: no sound device\n");
        return 1;
    }
    int format = AFMT_S16_LE;
    int channels = 1;
    int rate = 48000;
    ioctl(fd, SNDCTL_DSP_SETFMT, &format);
    ioctl(fd, SNDCTL_DSP_CHANNELS, &channels);
    ioctl(fd, SNDCTL_DSP_SPEED, &rate);

    int total = rate * ms / 1000;
    int fade = rate * FADE_MS / 1000;
    int16_t buf[512];
    for (int done = 0; done < total;)
    {
        int n = total - done < 512 ? total - done : 512;
        for (int i = 0; i < n; i++)
        {
            int t = done + i;
            double gain = 1.0;
            if (t < fade)
                gain = (double)t / fade;
            else if (total - t < fade)
                gain = (double)(total - t) / fade;
            buf[i] = (int16_t)(sin(2 * PI * hz * t / rate) * AMPLITUDE * gain);
        }
        if (write(fd, buf, (size_t)n * 2) != (ssize_t)n * 2)
            break;
        done += n;
    }
    close(fd);
    return 0;
}

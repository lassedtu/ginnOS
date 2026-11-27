<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/soundcard.h&gt;

Source: [`src/libc/include/sys/soundcard.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/soundcard.h)

the OSS sound interface: ioctl numbers and formats (ABI).

the numbers are the ones of the Open Sound System on Linux, so ported
programs find the names they know. the kernel has a copy in
src/kernel/audio/soundcard.h; the two must stay the same.

## audio\_buf\_info

```c
typedef struct
{
    int32_t fragments;  // whole free fragments
    int32_t fragstotal; // fragments in the buffer
    int32_t fragsize;   // bytes per fragment
    int32_t bytes;      // free bytes
} audio_buf_info; // NOLINT(readability-identifier-naming): the OSS name
```

the free space for SNDCTL\_DSP\_GETOSPACE, in bytes of the caller's format.

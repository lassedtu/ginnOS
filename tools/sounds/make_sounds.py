#!/usr/bin/env python3
"""
make the system sounds for /usr/share/sounds (WAV files, made here so the
repository holds no binary audio):

  chime.wav   22050 Hz, 8-bit mono: two notes (C6, then G6) that fade out
  stereo.wav  44100 Hz, 16-bit stereo: 440 Hz on the left, then 660 Hz on
              the right (0.5 s each), to check the channels

usage: make_sounds.py <output directory>
"""

import math
import os
import struct
import sys


def write_wav(path, rate, channels, bits, frames):
    """frames: a list of tuples of samples from -1.0 to 1.0, one per channel."""
    data = bytearray()
    for frame in frames:
        for sample in frame:
            sample = max(-1.0, min(1.0, sample))
            if bits == 8:
                data.append(int(round(sample * 127)) + 128)
            else:
                data += struct.pack('<h', int(round(sample * 32767)))
    block = channels * bits // 8
    header = b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE'
    header += b'fmt ' + struct.pack('<IHHIIHH', 16, 1, channels, rate, rate * block, block, bits)
    header += b'data' + struct.pack('<I', len(data))
    with open(path, 'wb') as f:
        f.write(header + data)


def note(rate, hz, seconds, volume):
    count = int(rate * seconds)
    return [math.sin(2 * math.pi * hz * i / rate) * volume * math.exp(-4.0 * i / count)
            for i in range(count)]


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)

    rate = 22050
    chime = note(rate, 1046.5, 0.25, 0.6) + note(rate, 1568.0, 0.5, 0.6)
    write_wav(os.path.join(out, 'chime.wav'), rate, 1, 8, [(s,) for s in chime])

    rate = 44100
    half = int(rate * 0.5)
    frames = [(0.4 * math.sin(2 * math.pi * 440 * i / rate), 0.0) for i in range(half)]
    frames += [(0.0, 0.4 * math.sin(2 * math.pi * 660 * i / rate)) for i in range(half)]
    write_wav(os.path.join(out, 'stereo.wav'), rate, 2, 16, frames)


if __name__ == '__main__':
    main()

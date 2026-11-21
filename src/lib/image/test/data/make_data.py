#!/usr/bin/env python3
"""make the test images for the image library tests (standard library only).

alpha.png: 8 x 8 RGBA, pixel (x, y) = (x*32, y*32, 128, (x+y)*16)
gray.png:  4 x 4 grey, value (x + 4*y) * 16
photo.ppm: 64 x 48 smooth colours; with --jpeg, photo.jpg is made from it
           with sips (macOS). both are committed, so the tests need no sips.
"""

import struct
import subprocess
import sys
import zlib


def png(path, width, height, colour_type, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    header = struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    png("alpha.png", 8, 8, 6,
        [[v for x in range(8) for v in (x * 32, y * 32, 128, (x + y) * 16)] for y in range(8)])
    png("gray.png", 4, 4, 0, [[(x + 4 * y) * 16 for x in range(4)] for y in range(4)])

    with open("photo.ppm", "wb") as f:
        f.write(b"P6\n64 48\n255\n")
        for y in range(48):
            for x in range(64):
                f.write(bytes((x * 4, y * 5, 255 - x * 2)))
    if "--jpeg" in sys.argv:
        subprocess.run(["sips", "-s", "format", "jpeg", "-s", "formatOptions", "90",
                        "photo.ppm", "--out", "photo.jpg"], check=True, capture_output=True,
                       stdin=subprocess.DEVNULL, timeout=60)


main()

#!/usr/bin/env python3
"""put the reference comparisons of a host test side by side.

usage: report.py <test output directory> <output .png> [scale]

hosttest_compare writes <name>-ref.ppm, <name>.ppm and <name>-diff.ppm.
this tool makes one picture: a row per cell with the reference (Chrome),
idunn's drawing and the difference (8 times brighter), magnified.
"""

import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import refpng  # noqa: E402

GAP = 6


def read_ppm(path):
    """a binary PPM ("P6", the width, the height, 255, one space, then the
    pixels). the pixels can start with bytes that look like spaces, so the
    header is read field by field."""
    data = open(path, "rb").read()
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1  # the one space after 255
    width, height = int(fields[1]), int(fields[2])
    return width, height, data[pos:pos + width * height * 3]


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit("usage: report.py <test output directory> <output .png> [scale]")
    scale = int(sys.argv[3]) if len(sys.argv) == 4 else 2
    names = sorted(os.path.basename(p)[:-8] for p in glob.glob(os.path.join(sys.argv[1], "*-ref.ppm")))
    if not names:
        sys.exit("report: no comparisons in " + sys.argv[1])
    rows = []
    for name in names:
        rows.append([read_ppm(os.path.join(sys.argv[1], name + suffix))
                     for suffix in ("-ref.ppm", ".ppm", "-diff.ppm")])

    width = max(sum(w * scale for w, _, _ in row) + GAP * 4 for row in rows)
    height = sum(max(h for _, h, _ in row) * scale + GAP for row in rows) + GAP
    canvas = bytearray(b"\x80" * (width * height * 3))
    y0 = GAP
    for row in rows:
        x0 = GAP
        for w, h, pixels in row:
            for y in range(h * scale):
                for x in range(w * scale):
                    src = ((y // scale) * w + x // scale) * 3
                    dst = ((y0 + y) * width + x0 + x) * 3
                    canvas[dst:dst + 3] = pixels[src:src + 3]
            x0 += w * scale + GAP
        y0 += max(h for _, h, _ in row) * scale + GAP
    refpng.write_png(sys.argv[2], width, height, bytes(canvas))
    print("report: %s (%s)" % (sys.argv[2], ", ".join(names)))


main()

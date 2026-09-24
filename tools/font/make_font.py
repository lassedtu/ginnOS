#!/usr/bin/env python3
"""
Generate the bifrost framebuffer font as a C header by rasterizing a TrueType
font (JetBrains Mono by default) to a fixed-size monochrome bitmap.

The kernel stays a simple bitmap blitter: this tool does the rasterization
offline and bakes the result into a font_glyphs[256][H][bytes_per_row] array.
No TrueType rasterizer runs in the kernel.

Layout emitted (matches fb_text renderer expectations):
  font_glyphs[glyph][row][byte], where each row is ceil(width/8) bytes and
  the most significant bit of byte 0 is the leftmost pixel.

Usage:
  make_font.py [--font PATH] [--size N] [--cell WxH] [--yoff N] OUTPUT.h

Defaults: JetBrains Mono Regular, size 16, cell 10x20, yoff 0.
Requires Pillow (PIL). Run offline; the generated header is committed so the
kernel build itself has no font dependency.

JetBrains Mono is licensed under the SIL Open Font License 1.1; the notice is
copied into the generated header.
"""

import argparse
import os
import sys

try:
    from PIL import Image, ImageFont, ImageDraw
except ImportError:
    sys.stderr.write("error: Pillow (PIL) is required: pip install Pillow\n")
    sys.exit(1)

GLYPH_COUNT = 256

DEFAULT_FONT = os.path.expanduser("~/Library/Fonts/JetBrainsMono-Regular.ttf")

OFL_NOTICE = (
    "JetBrains Mono, Copyright 2020 The JetBrains Mono Project Authors, "
    "licensed under the SIL Open Font License, Version 1.1. "
    "Rasterized to a bitmap by tools/font/make_font.py."
)


def rasterize(font_path, size, cell_w, cell_h, yoff):
    font = ImageFont.truetype(font_path, size)
    bytes_per_row = (cell_w + 7) // 8
    glyphs = []
    for cp in range(GLYPH_COUNT):
        img = Image.new("L", (cell_w, cell_h), 0)
        # printable range only; control chars stay blank.
        if 32 <= cp < 127 or 160 <= cp < 256:
            ch = chr(cp)
            ImageDraw.Draw(img).text((1, yoff), ch, fill=255, font=font)
        rows = []
        for y in range(cell_h):
            row_bytes = [0] * bytes_per_row
            for x in range(cell_w):
                if img.getpixel((x, y)) > 110:
                    row_bytes[x // 8] |= 1 << (7 - (x % 8))
            rows.append(row_bytes)
        glyphs.append(rows)
    return glyphs, bytes_per_row


def emit_header(glyphs, cell_w, cell_h, bytes_per_row, out):
    out.write("#pragma once\n\n")
    out.write("/**\n")
    out.write(" * @file font.h\n")
    out.write(" * @brief generated bitmap font for the framebuffer text renderer.\n")
    out.write(" *\n")
    out.write(" * do not edit by hand. regenerate with tools/font/make_font.py.\n")
    out.write(" * layout: font_glyphs[glyph][row][byte]; each row is\n")
    out.write(" * FONT_GLYPH_BYTES_PER_ROW bytes, msb of byte 0 = leftmost pixel.\n")
    out.write(" *\n")
    out.write(" * %s\n" % OFL_NOTICE)
    out.write(" */\n\n")
    out.write('#include "common/stdint.h"\n\n')
    out.write("#define FONT_GLYPH_WIDTH        %d\n" % cell_w)
    out.write("#define FONT_GLYPH_HEIGHT       %d\n" % cell_h)
    out.write("#define FONT_GLYPH_BYTES_PER_ROW %d\n" % bytes_per_row)
    out.write("#define FONT_GLYPH_COUNT        %d\n\n" % GLYPH_COUNT)
    out.write("static const uint8_t font_glyphs"
              "[FONT_GLYPH_COUNT][FONT_GLYPH_HEIGHT][FONT_GLYPH_BYTES_PER_ROW] = {\n")
    for cp in range(GLYPH_COUNT):
        out.write("    [%d] = {" % cp)
        row_strs = []
        for row in glyphs[cp]:
            row_strs.append("{" + ",".join("0x%02X" % b for b in row) + "}")
        out.write(",".join(row_strs))
        out.write("},\n")
    out.write("};\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("output")
    ap.add_argument("--font", default=DEFAULT_FONT)
    ap.add_argument("--size", type=int, default=16)
    ap.add_argument("--cell", default="10x20")
    ap.add_argument("--yoff", type=int, default=0)
    args = ap.parse_args()

    cell_w, cell_h = (int(v) for v in args.cell.lower().split("x"))

    if not os.path.exists(args.font):
        sys.stderr.write("error: font not found: %s\n" % args.font)
        sys.exit(1)

    glyphs, bpr = rasterize(args.font, args.size, cell_w, cell_h, args.yoff)
    with open(args.output, "w") as f:
        emit_header(glyphs, cell_w, cell_h, bpr, f)
    sys.stderr.write("wrote %s (%dx%d, %d bytes/row)\n"
                     % (args.output, cell_w, cell_h, bpr))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""rasterize the listed Lucide and Tela icons into ginnOS icon packs.

usage: build.py <source directory> <output directory>

the source directory is the one tools/icons/fetch.sh fills (src/lucide and
src/tela in its cache). the output gets lucide.pack, tela.pack and the two
licences. the SVG files are drawn by rsvg-convert (or resvg), so ginnOS
needs no SVG code.

the pack format (all numbers little-endian):

    header, 16 bytes:
        char     magic[4]     "GIPK"
        uint16   version      1
        uint8    kind         0: masks (1 byte per pixel, coverage)
                              1: premultiplied ARGB (uint32 0xAARRGGBB)
        uint8    reserved     0
        uint32   count        entries in the index
        uint32   reserved     0
    index, count entries of 48 bytes, sorted by name, size, scale:
        char     name[32]     zero-padded
        uint16   size         the icon size (16, 20, 24, 32, 48, 64)
        uint8    scale        1 or 2 (the image is size * scale pixels)
        uint8    reserved     0
        uint16   width
        uint16   height
        uint32   offset       from the start of the file
        uint32   length       bytes of zlib data
    then the images, each compressed with zlib.

Lucide icons are drawn with stroke width 1.5 instead of 2 at sizes 16 and
20 (2 looks heavy when small).
"""

import concurrent.futures
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

SIZES = (16, 20, 24, 32, 48, 64)
SCALES = (1, 2)
THIN_SIZES = (16, 20)
TELA_DIRS = ("apps", "places", "mimetypes", "devices")
NAME_MAX = 31


def die(message):
    print("icons: " + message, file=sys.stderr)
    sys.exit(1)


def renderer():
    for tool in ("resvg", "rsvg-convert"):
        if shutil.which(tool):
            return tool
    die("rsvg-convert or resvg is needed to draw the icons "
        "(macOS: brew install librsvg; Debian: apt install librsvg2-bin)")
    return None


def render(tool, svg, pixels, png):
    if tool == "resvg":
        cmd = ["resvg", "-w", str(pixels), "-h", str(pixels), svg, png]
    else:
        cmd = ["rsvg-convert", "-w", str(pixels), "-h", str(pixels), svg, "-o", png]
    subprocess.run(cmd, check=True, stdin=subprocess.DEVNULL, capture_output=True)


def read_png(path):
    """decode an 8-bit RGBA or RGB PNG (what the renderers write)."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        die(path + ": not a PNG")
    pos = 8
    idat = b""
    width = height = channels = 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or colour not in (2, 6) or interlace:
                die(path + ": an unexpected PNG format")
            channels = 4 if colour == 6 else 3
        elif kind == b"IDAT":
            idat += chunk
    raw = zlib.decompress(idat)
    stride = width * channels
    prev = bytearray(stride)
    rows = []
    i = 0
    for _ in range(height):
        kind = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    pixels = []
    for row in rows:
        for x in range(width):
            px = row[x * channels:(x + 1) * channels]
            pixels.append(tuple(px) if channels == 4 else tuple(px) + (255,))
    return width, height, pixels


def read_list(path):
    names = []
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if line:
            if len(line) > NAME_MAX:
                die(f"{path}: the name {line} is longer than {NAME_MAX} characters")
            names.append(line)
    return names


def lucide_svg(src, name, size, scale, tmp):
    path = os.path.join(src, "lucide", "icons", name + ".svg")
    if not os.path.isfile(path):
        die("Lucide has no icon named " + name)
    if size not in THIN_SIZES:
        return path
    text = open(path).read().replace('stroke-width="2"', 'stroke-width="1.5"')
    thin = os.path.join(tmp, f"{name}-thin.svg")
    with open(thin, "w") as f:
        f.write(text)
    return thin


def tela_svg(src, name):
    """the SVG of a Tela name: in src/scalable, or a link in links/scalable
    to a file in src/scalable (the install script puts both together)."""
    base = os.path.join(src, "tela")
    for group in TELA_DIRS:
        path = os.path.join(base, "src", "scalable", group, name + ".svg")
        if os.path.isfile(path):
            return path
        link = os.path.join(base, "links", "scalable", group, name + ".svg")
        if os.path.islink(link):
            target = os.path.basename(os.readlink(link))
            path = os.path.join(base, "src", "scalable", group, target)
            if os.path.isfile(path):
                return path
    die("Tela has no icon named " + name)
    return None


def write_pack(path, kind, entries):
    """entries: (name, size, scale, width, height, raw bytes)."""
    entries.sort(key=lambda e: (e[0].encode(), e[1], e[2]))
    offset = 16 + 48 * len(entries)
    index = b""
    blobs = []
    for name, size, scale, width, height, raw in entries:
        blob = zlib.compress(raw, 9)
        index += struct.pack("<32sHBBHHII", name.encode(), size, scale, 0, width, height,
                             offset, len(blob))
        blobs.append(blob)
        offset += len(blob)
    with open(path, "wb") as f:
        f.write(b"GIPK" + struct.pack("<HBBII", 1, kind, 0, len(entries), 0))
        f.write(index)
        for blob in blobs:
            f.write(blob)


def mask_bytes(pixels):
    return bytes(a for (_, _, _, a) in pixels)


def argb_bytes(pixels):
    out = bytearray()
    for r, g, b, a in pixels:
        pr = (r * a + 127) // 255
        pg = (g * a + 127) // 255
        pb = (b * a + 127) // 255
        out += struct.pack("<I", a << 24 | pr << 16 | pg << 8 | pb)
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        die("usage: build.py <source directory> <output directory>")
    src, out = sys.argv[1], sys.argv[2]
    here = os.path.dirname(os.path.abspath(__file__))
    tool = renderer()
    os.makedirs(out, exist_ok=True)

    lucide = read_list(os.path.join(here, "lucide.list"))
    tela = read_list(os.path.join(here, "tela.list"))

    with tempfile.TemporaryDirectory() as tmp:
        jobs = []  # (set, name, size, scale, svg, png)
        for name in lucide:
            for size in SIZES:
                for scale in SCALES:
                    svg = lucide_svg(src, name, size, scale, tmp)
                    jobs.append(("lucide", name, size, scale, svg,
                                 os.path.join(tmp, f"l-{name}-{size}-{scale}.png")))
        for name in tela:
            svg = tela_svg(src, name)
            for size in SIZES:
                for scale in SCALES:
                    jobs.append(("tela", name, size, scale, svg,
                                 os.path.join(tmp, f"t-{name}-{size}-{scale}.png")))

        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            list(pool.map(lambda j: render(tool, j[4], j[2] * j[3], j[5]), jobs))

        packs = {"lucide": [], "tela": []}
        for kind, name, size, scale, _, png in jobs:
            width, height, pixels = read_png(png)
            raw = mask_bytes(pixels) if kind == "lucide" else argb_bytes(pixels)
            packs[kind].append((name, size, scale, width, height, raw))

    write_pack(os.path.join(out, "lucide.pack"), 0, packs["lucide"])
    write_pack(os.path.join(out, "tela.pack"), 1, packs["tela"])
    shutil.copyfile(os.path.join(src, "lucide", "LICENSE"), os.path.join(out, "LICENSE-lucide.txt"))
    shutil.copyfile(os.path.join(src, "tela", "COPYING"), os.path.join(out, "COPYING-tela.txt"))
    for name in ("lucide.pack", "tela.pack"):
        print(f"icons: {name}: {os.path.getsize(os.path.join(out, name)) // 1024} KiB")


main()

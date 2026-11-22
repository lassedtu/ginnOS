"""helpers for the idunn reference tools: render HTML with headless Chrome
and read the PNG it writes (no packages needed)."""

import os
import shutil
import struct
import subprocess
import sys
import zlib

CHROME_PATHS = (
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "google-chrome",
    "chromium",
    "chromium-browser",
)


def chrome():
    for path in CHROME_PATHS:
        if os.path.isfile(path) or shutil.which(path):
            return path
    sys.exit("idunn-ref: Google Chrome or Chromium is needed (set CHROME=...)")


def render(html_path, png_path, width, height, scale=1):
    """render html_path at a device scale factor into png_path."""
    exe = os.environ.get("CHROME") or chrome()
    cmd = [exe, "--headless=new", "--disable-gpu", "--hide-scrollbars",
           "--force-device-scale-factor=%d" % scale, "--allow-file-access-from-files",
           "--window-size=%d,%d" % (width, height), "--screenshot=" + png_path,
           "file://" + os.path.abspath(html_path)]
    subprocess.run(cmd, check=True, stdin=subprocess.DEVNULL, capture_output=True)


class Image:
    """an 8-bit RGB or RGBA PNG, as rows of bytes."""

    def __init__(self, path):
        data = open(path, "rb").read()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            sys.exit(path + ": not a PNG")
        pos = 8
        idat = b""
        while pos < len(data):
            (length,) = struct.unpack(">I", data[pos:pos + 4])
            kind = data[pos + 4:pos + 8]
            chunk = data[pos + 8:pos + 8 + length]
            pos += 12 + length
            if kind == b"IHDR":
                self.width, self.height, depth, colour = struct.unpack(">IIBB", chunk[:10])
                if depth != 8 or colour not in (2, 6):
                    sys.exit(path + ": an unexpected PNG format")
                self.channels = 4 if colour == 6 else 3
            elif kind == b"IDAT":
                idat += chunk
        raw = zlib.decompress(idat)
        ch = self.channels
        stride = self.width * ch
        prev = bytearray(stride)
        self.rows = []
        i = 0
        for _ in range(self.height):
            kind = raw[i]
            line = bytearray(raw[i + 1:i + 1 + stride])
            i += 1 + stride
            for x in range(stride):
                a = line[x - ch] if x >= ch else 0
                b = prev[x]
                c = prev[x - ch] if x >= ch else 0
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
            self.rows.append(bytes(line))
            prev = line

    def rgb(self, x, y):
        o = x * self.channels
        return tuple(self.rows[y][o:o + 3])


def write_png(path, width, height, rgb):
    """write 8-bit RGB pixels (bytes, rows from the top) as a PNG."""
    raw = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(kind, data):
        crc = zlib.crc32(kind + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) +
                chunk(b"IEND", b""))

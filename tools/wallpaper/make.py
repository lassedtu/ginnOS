#!/usr/bin/env python3
"""
make the default wallpaper (/usr/share/wallpapers/default.png) from an
image: it is put on black (its transparent parts too), scaled to 1024
pixels high, and given black sides to 1638 by 1024 (16:10). heimdalld
scales a wallpaper to cover the screen, so the image fills the height on a
16:10 screen and on a 5:4 screen only the black sides are cut.

the image is the user's own (git ignores .hidden), so it is not in the
repository. without the image or without Pillow, nothing is made and
heimdalld draws its gradient.

usage: make.py <image> <output.png>
"""

import os
import sys

WIDTH = 1638
HEIGHT = 1024


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    source, output = sys.argv[1], sys.argv[2]
    if not os.path.isfile(source):
        print("wallpaper: %s not found, heimdalld draws its gradient" % source)
        return
    try:
        from PIL import Image
    except ImportError:
        print("wallpaper: Pillow is not installed, heimdalld draws its gradient")
        return

    image = Image.open(source).convert("RGBA")
    on_black = Image.new("RGBA", image.size, (0, 0, 0, 255))
    on_black.alpha_composite(image)
    scale = HEIGHT / on_black.height
    size = (round(on_black.width * scale), HEIGHT)
    fitted = on_black.convert("RGB").resize(size, Image.LANCZOS)
    if fitted.width > WIDTH:
        left = (fitted.width - WIDTH) // 2
        fitted = fitted.crop((left, 0, left + WIDTH, HEIGHT))

    canvas = Image.new("RGB", (WIDTH, HEIGHT), (0, 0, 0))
    canvas.paste(fitted, ((WIDTH - fitted.width) // 2, 0))
    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
    canvas.save(output, optimize=True)
    print("wallpaper: %s -> %s" % (source, output))


if __name__ == "__main__":
    main()

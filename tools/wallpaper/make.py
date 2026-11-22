#!/usr/bin/env python3
"""
make the default wallpaper (/usr/share/wallpapers/default.png) from an
image: it is put on black (its transparent parts too), scaled to fit the
screen, and given black sides. at the screen size heimdalld shows it pixel
for pixel. without a size it is 1638 by 1024 (16:10), which heimdalld
scales to cover any screen; on a 5:4 screen only the black sides are cut.

the image is the user's own (git ignores .hidden), so it is not in the
repository. without the image or without Pillow, nothing is made and
heimdalld draws its gradient.

usage: make.py <image> <output.png> [<width>x<height>]
"""

import os
import sys

SIZE = (1638, 1024)


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    source, output = sys.argv[1], sys.argv[2]
    width, height = SIZE
    if len(sys.argv) == 4:
        try:
            width, height = (int(n) for n in sys.argv[3].lower().split("x"))
        except ValueError:
            sys.exit("wallpaper: the size is <width>x<height>, not %s" % sys.argv[3])
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
    scale = min(width / on_black.width, height / on_black.height)
    size = (round(on_black.width * scale), round(on_black.height * scale))
    fitted = on_black.convert("RGB").resize(size, Image.LANCZOS)

    canvas = Image.new("RGB", (width, height), (0, 0, 0))
    canvas.paste(fitted, ((width - fitted.width) // 2, (height - fitted.height) // 2))
    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
    canvas.save(output, optimize=True)
    print("wallpaper: %s -> %s (%dx%d)" % (source, output, width, height))


if __name__ == "__main__":
    main()

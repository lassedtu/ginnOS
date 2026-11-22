#!/usr/bin/env python3
"""render the idunn reference cells with headless Chrome.

usage: spec.py <output directory>

each cell is a small piece of the agreed look (plans/idunn-ui-plan.md),
drawn with CSS: a background, and one element at a place, with the real
token values. the host tests draw the same cells with libgfx and compare
them with these PNGs (a yardstick for "as close as we can get", with a
tolerance, not a pixel lock).

the cells use the same Inter files as ginnOS (build/rootfs/usr/share/
fonts), so text can be compared too.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import refpng  # noqa: E402

GAP = 8  # between the cells on the sheet

# name, width, height, page background, the element's CSS
CELLS = [
    ("fill", 120, 80, "#09090b",
     "left:24px;top:24px;width:72px;height:32px;border-radius:6px;background:#e11d48"),
    ("gradient", 120, 80, "#09090b",
     "left:24px;top:24px;width:72px;height:32px;border-radius:6px;"
     "background:linear-gradient(#e11d48,#3b82f6)"),
    ("border", 120, 80, "#ffffff",
     "left:24px;top:24px;width:72px;height:32px;border-radius:6px;border:1px solid #e4e4e7"),
    ("card", 120, 80, "#09090b",
     "left:24px;top:24px;width:72px;height:32px;border-radius:10px;background:#18181b;"
     "border:1px solid rgb(255 255 255 / 0.1)"),
    ("focus", 120, 80, "#ffffff",
     "left:24px;top:24px;width:72px;height:32px;border-radius:6px;background:#ffffff;"
     "border:1px solid #e11d48;box-shadow:0 0 0 3px rgb(225 29 72 / 0.5)"),
    ("shadow", 160, 120, "#fbfbfb",
     "left:40px;top:32px;width:80px;height:48px;border-radius:6px;background:#ffffff;"
     "box-shadow:0 8px 20px -4px rgb(0 0 0 / 0.14)"),
    ("window", 220, 200, "#4a3f78",
     "left:50px;top:40px;width:120px;height:90px;border-radius:10px;background:#09090b;"
     "box-shadow:0 10px 22px -6px rgb(0 0 0 / 0.5), 0 28px 56px -16px rgb(0 0 0 / 0.65)"),
    ("inset", 120, 80, "#ffffff",
     "left:24px;top:24px;width:72px;height:32px;border-radius:6px;background:#ffffff;"
     "border:1px solid #e4e4e7;box-shadow:inset 0 1px 2px rgb(0 0 0 / 0.12)"),
]


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: spec.py <output directory>")
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)

    # one row of cells on a sheet
    sheet_w = sum(w for _, w, _, _, _ in CELLS) + GAP * (len(CELLS) + 1)
    sheet_h = max(h for _, _, h, _, _ in CELLS) + 2 * GAP
    parts = ['<!doctype html><meta charset="utf-8"><style>'
             'html,body{margin:0;background:#000}'
             '.cell{position:absolute;overflow:hidden}'
             '.cell>div{position:absolute;box-sizing:border-box}'
             '</style>']
    places = []
    x = GAP
    for name, w, h, bg, css in CELLS:
        parts.append('<div class="cell" style="left:%dpx;top:%dpx;width:%dpx;height:%dpx;'
                     'background:%s"><div style="%s"></div></div>' % (x, GAP, w, h, bg, css))
        places.append((name, x, GAP, w, h))
        x += w + GAP

    with tempfile.TemporaryDirectory() as tmp:
        page = os.path.join(tmp, "spec.html")
        png = os.path.join(tmp, "spec.png")
        with open(page, "w") as f:
            f.write("".join(parts))
        refpng.render(page, png, sheet_w, sheet_h)
        sheet = refpng.Image(png)

    for name, x0, y0, w, h in places:
        rgb = bytearray()
        for y in range(h):
            for x in range(w):
                rgb += bytes(sheet.rgb(x0 + x, y0 + y))
        refpng.write_png(os.path.join(out_dir, name + ".png"), w, h, bytes(rgb))
    print("spec: %d cells in %s" % (len(CELLS), out_dir))


main()

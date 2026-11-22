#!/usr/bin/env python3
"""render the idunn reference cells with headless Chrome.

usage: spec.py

writes src/lib/<library>/test/reference/<cell>.png for each cell.

each cell is a small piece of the agreed look (plans/idunn-ui-plan.md),
drawn with CSS: a background, and one element at a place, with the real
token values. the host tests draw the same cells with libgfx and compare
them with these PNGs (a yardstick for "as close as we can get", with a
tolerance, not a pixel lock).

the text cells use the same Inter files as ginnOS (the ones the host
tests use, build/host-test/fonts: run make host-test first), so text can
be compared too.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import refpng  # noqa: E402

GAP = 8  # between the cells on the sheet
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
FONT_DIR = os.path.join(ROOT, "build", "host-test", "fonts", "inter")
TEXT = ("-webkit-font-smoothing:antialiased;white-space:nowrap;")

# library, name, width, height, page background, the element's CSS, text
CELLS = [
    ("gfx", "fill", 120, 80, "#09090b",
     "left:24px;top:24px;width:72px;height:32px;border-radius:6px;background:#e11d48", ""),
    ("gfx", "gradient", 120, 80, "#09090b",
     "left:24px;top:24px;width:72px;height:32px;border-radius:6px;"
     "background:linear-gradient(#e11d48,#3b82f6)", ""),
    ("gfx", "border", 120, 80, "#ffffff",
     "left:24px;top:24px;width:72px;height:32px;border-radius:6px;border:1px solid #e4e4e7", ""),
    ("gfx", "card", 120, 80, "#09090b",
     "left:24px;top:24px;width:72px;height:32px;border-radius:10px;background:#18181b;"
     "border:1px solid rgb(255 255 255 / 0.1)", ""),
    ("gfx", "focus", 120, 80, "#ffffff",
     "left:24px;top:24px;width:72px;height:32px;border-radius:6px;background:#ffffff;"
     "border:1px solid #e11d48;box-shadow:0 0 0 3px rgb(225 29 72 / 0.5)", ""),
    ("gfx", "shadow", 160, 120, "#fbfbfb",
     "left:40px;top:32px;width:80px;height:48px;border-radius:6px;background:#ffffff;"
     "box-shadow:0 8px 20px -4px rgb(0 0 0 / 0.14)", ""),
    ("gfx", "window", 220, 200, "#4a3f78",
     "left:50px;top:40px;width:120px;height:90px;border-radius:10px;background:#09090b;"
     "box-shadow:0 10px 22px -6px rgb(0 0 0 / 0.5), 0 28px 56px -16px rgb(0 0 0 / 0.65)", ""),
    ("gfx", "inset", 120, 80, "#ffffff",
     "left:24px;top:24px;width:72px;height:32px;border-radius:6px;background:#ffffff;"
     "border:1px solid #e4e4e7;box-shadow:inset 0 1px 2px rgb(0 0 0 / 0.12)", ""),
    # text: the line box starts at (12, 11); the styles of plan section 2.3
    ("bragi", "label", 140, 40, "#09090b",
     "left:12px;top:11px;font:500 13px/18px Inter;letter-spacing:-0.006em;color:#fafafa;" + TEXT,
     "Apply changes"),
    ("bragi", "body", 140, 40, "#ffffff",
     "left:12px;top:11px;font:400 13px/18px Inter;letter-spacing:-0.006em;color:#09090b;" + TEXT,
     "Find a file"),
    ("bragi", "heading", 160, 48, "#09090b",
     "left:12px;top:11px;font:600 19px/26px Inter;letter-spacing:-0.02em;color:#fafafa;" + TEXT,
     "Appearance"),
]


def font_faces():
    """the Inter files ginnOS uses, as CSS font faces."""
    faces = []
    for weight, name in ((400, "Regular"), (500, "Medium"), (600, "SemiBold")):
        path = os.path.join(FONT_DIR, "Inter-%s.ttf" % name)
        if not os.path.isfile(path):
            sys.exit("spec: %s is missing (run make host-test first)" % path)
        faces.append('@font-face{font-family:Inter;font-weight:%d;src:url("file://%s")}'
                     % (weight, path))
    return "".join(faces)


def main():
    if len(sys.argv) != 1:
        sys.exit("usage: spec.py")

    # one row of cells on a sheet
    sheet_w = sum(c[2] for c in CELLS) + GAP * (len(CELLS) + 1)
    sheet_h = max(c[3] for c in CELLS) + 2 * GAP
    parts = ['<!doctype html><meta charset="utf-8"><style>' + font_faces() +
             'html,body{margin:0;background:#000}'
             '.cell{position:absolute;overflow:hidden}'
             '.cell>div{position:absolute;box-sizing:border-box}'
             '</style>']
    places = []
    x = GAP
    for lib, name, w, h, bg, css, text in CELLS:
        parts.append('<div class="cell" style="left:%dpx;top:%dpx;width:%dpx;height:%dpx;'
                     'background:%s"><div style="%s">%s</div></div>' % (x, GAP, w, h, bg, css, text))
        places.append((lib, name, x, GAP, w, h))
        x += w + GAP

    with tempfile.TemporaryDirectory() as tmp:
        page = os.path.join(tmp, "spec.html")
        png = os.path.join(tmp, "spec.png")
        with open(page, "w") as f:
            f.write("".join(parts))
        refpng.render(page, png, sheet_w, sheet_h)
        sheet = refpng.Image(png)

    for lib, name, x0, y0, w, h in places:
        out_dir = os.path.join(ROOT, "src", "lib", lib, "test", "reference")
        os.makedirs(out_dir, exist_ok=True)
        rgb = bytearray()
        for y in range(h):
            for x in range(w):
                rgb += bytes(sheet.rgb(x0 + x, y0 + y))
        refpng.write_png(os.path.join(out_dir, name + ".png"), w, h, bytes(rgb))
    print("spec: %d cells" % len(CELLS))

main()

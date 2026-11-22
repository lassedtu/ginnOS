#!/usr/bin/env python3
"""render the idunn reference cells with headless Chrome.

usage: spec.py

writes src/lib/<library>/test/reference/<cell>.png for each cell.

each cell is a small piece of the agreed look (plans/idunn-ui-plan.md),
drawn with CSS: a background and an element or a row of them, with the
real token values. the host tests draw the same cells with libgfx and compare
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

def box(css, text=""):
    """one absolutely placed element."""
    return '<div class="box" style="%s">%s</div>' % (css, text)


# the buttons of the preview (its CSS, with the agreed tokens: 32 px,
# 13 px text, 6 px radius, the refined finish and soft red)
BUTTON_CSS = """
.buttons{position:absolute;left:0;top:0;display:flex;gap:8px;padding:16px;align-items:center;
  font:500 13px/18px Inter;letter-spacing:-0.006em;-webkit-font-smoothing:antialiased}
.btn{height:32px;padding:0 16px;display:inline-flex;align-items:center;justify-content:center;
  border-radius:6px;border:1px solid transparent;white-space:nowrap;box-sizing:border-box}
.light{--bg:#fff;--fg:#09090b;--card:#fff;--hover:#f4f4f5;--input:#e4e4e7;--primary:#e11d48;
  --primary-fg:#fff;--secondary:#f4f4f5;--secondary-fg:#18181b;--destructive:#dc2626}
.dark{--bg:#09090b;--fg:#fafafa;--card:#18181b;--hover:#27272a;--input:rgb(255 255 255/.15);
  --primary:#f43f5e;--primary-fg:#fff;--secondary:#27272a;--secondary-fg:#fafafa;
  --destructive:#ef4444}
.primary{background:var(--primary);color:var(--primary-fg);
  background-image:linear-gradient(rgb(255 255 255/.08),rgb(255 255 255/0));
  box-shadow:inset 0 1px 0 rgb(255 255 255/.14),0 1px 2px rgb(0 0 0/.14),
  0 0 0 1px color-mix(in srgb,var(--primary) 85%,#000)}
.secondary{background:var(--secondary);color:var(--secondary-fg)}
.outline{background:var(--card);color:var(--fg);border-color:var(--input)}
.light .outline{box-shadow:0 1px 2px rgb(0 0 0/.05),inset 0 -1px 0 rgb(0 0 0/.03)}
.dark .outline{box-shadow:inset 0 1px 0 rgb(255 255 255/.04),0 1px 2px rgb(0 0 0/.3)}
.ghost{background:transparent;color:var(--fg)}
.destructive{background:color-mix(in srgb,var(--destructive) 12%,var(--card));
  color:var(--destructive);box-shadow:inset 0 0 0 1px color-mix(in srgb,var(--destructive) 25%,transparent)}
.link{background:transparent;color:var(--fg);padding:0 4px}
"""


# the inputs of the preview (refined finish)
INPUT_CSS = """
.inputs{position:absolute;left:0;top:0;display:flex;gap:16px;padding:16px;align-items:center;
  font:400 13px/18px Inter;letter-spacing:-0.006em;-webkit-font-smoothing:antialiased}
.inputs *{box-sizing:border-box}
.light{--muted-fg:#71717a;--field:#fff;--check-border:#e4e4e7;--accent:#e11d48;--accent-fg:#fff;
  --xs:0 1px 2px rgb(0 0 0/.05);--switch-off:#e4e4e7}
.dark{--muted-fg:#a1a1aa;--field:#131315;--check-border:rgb(255 255 255/.25);--accent:#f43f5e;
  --accent-fg:#fff;--xs:0 1px 2px rgb(0 0 0/.3);--switch-off:rgb(255 255 255/.18)}
.fi{width:180px;height:32px;padding:0 12px;display:flex;align-items:center;border:1px solid var(--input);
  border-radius:6px;background:var(--field);color:var(--muted-fg)}
.light .fi{box-shadow:inset 0 1px 2px rgb(0 0 0/.04),0 1px 0 rgb(255 255 255/.6)}
.dark .fi{box-shadow:inset 0 1px 2px rgb(0 0 0/.3)}
.ck{width:16px;height:16px;border-radius:3px;border:1px solid var(--check-border);box-shadow:var(--xs);
  display:grid;place-items:center}
.ck.on{background:var(--accent);border-color:var(--accent)}
.ck svg{width:12px;height:12px;fill:none;stroke:var(--accent-fg);stroke-width:2.5;
  stroke-linecap:round;stroke-linejoin:round}
.rd{width:16px;height:16px;border-radius:50%;border:1px solid var(--accent);box-shadow:var(--xs);
  display:grid;place-items:center}
.rd::after{content:"";width:8px;height:8px;border-radius:50%;background:var(--accent)}
.sw{width:32px;height:18px;border-radius:999px;padding:0 2px;background:var(--switch-off);
  display:flex;align-items:center}
.sw.on{background:var(--accent);justify-content:flex-end}
.sw::after{content:"";width:14px;height:14px;border-radius:50%;background:#fff;
  box-shadow:0 1px 2px rgb(0 0 0/.25)}
"""


def inputs(mode):
    check = '<svg viewBox="0 0 24 24"><path d="M20 6 9 17l-5-5"/></svg>'
    return ('<div class="inputs %s"><span class="fi">Name</span><span class="ck on">%s</span>'
            '<span class="ck"></span><span class="rd"></span><span class="sw on"></span>'
            '<span class="sw"></span></div>' % (mode, check))


def buttons(mode):
    names = ("Primary", "Secondary", "Outline", "Ghost", "Delete", "Link")
    kinds = ("primary", "secondary", "outline", "ghost", "destructive", "link")
    inner = "".join('<span class="btn %s">%s</span>' % (k, n) for k, n in zip(kinds, names))
    return '<div class="buttons %s">%s</div>' % (mode, inner)


# library, name, width, height, page background, HTML
CELLS = [
    ("gfx", "fill", 120, 80, "#09090b",
     box("left:24px;top:24px;width:72px;height:32px;border-radius:6px;background:#e11d48")),
    ("gfx", "gradient", 120, 80, "#09090b",
     box("left:24px;top:24px;width:72px;height:32px;border-radius:6px;"
         "background:linear-gradient(#e11d48,#3b82f6)")),
    ("gfx", "border", 120, 80, "#ffffff",
     box("left:24px;top:24px;width:72px;height:32px;border-radius:6px;border:1px solid #e4e4e7")),
    ("gfx", "card", 120, 80, "#09090b",
     box("left:24px;top:24px;width:72px;height:32px;border-radius:10px;background:#18181b;"
         "border:1px solid rgb(255 255 255 / 0.1)")),
    ("gfx", "focus", 120, 80, "#ffffff",
     box("left:24px;top:24px;width:72px;height:32px;border-radius:6px;background:#ffffff;"
         "border:1px solid #e11d48;box-shadow:0 0 0 3px rgb(225 29 72 / 0.5)")),
    ("gfx", "shadow", 160, 120, "#fbfbfb",
     box("left:40px;top:32px;width:80px;height:48px;border-radius:6px;background:#ffffff;"
         "box-shadow:0 8px 20px -4px rgb(0 0 0 / 0.14)")),
    ("gfx", "window", 220, 200, "#4a3f78",
     box("left:50px;top:40px;width:120px;height:90px;border-radius:10px;background:#09090b;"
         "box-shadow:0 10px 22px -6px rgb(0 0 0 / 0.5), 0 28px 56px -16px rgb(0 0 0 / 0.65)")),
    ("gfx", "inset", 120, 80, "#ffffff",
     box("left:24px;top:24px;width:72px;height:32px;border-radius:6px;background:#ffffff;"
         "border:1px solid #e4e4e7;box-shadow:inset 0 1px 2px rgb(0 0 0 / 0.12)")),
    # text: the line box starts at (12, 11); the styles of plan section 2.3
    ("bragi", "label", 140, 40, "#09090b",
     box("left:12px;top:11px;font:500 13px/18px Inter;letter-spacing:-0.006em;color:#fafafa;" + TEXT,
         "Apply changes")),
    ("bragi", "body", 140, 40, "#ffffff",
     box("left:12px;top:11px;font:400 13px/18px Inter;letter-spacing:-0.006em;color:#09090b;" + TEXT,
         "Find a file")),
    ("bragi", "heading", 160, 48, "#09090b",
     box("left:12px;top:11px;font:600 19px/26px Inter;letter-spacing:-0.02em;color:#fafafa;" + TEXT,
         "Appearance")),
    # the button variants on the window's surface
    ("idunn", "buttons-dark", 520, 64, "#0e0e10", buttons("dark")),
    ("idunn", "buttons-light", 520, 64, "#fbfbfb", buttons("light")),
    # the inputs on the window's surface
    ("idunn", "inputs-dark", 400, 64, "#0e0e10", inputs("dark")),
    ("idunn", "inputs-light", 400, 64, "#fbfbfb", inputs("light")),
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
             '.box{position:absolute;box-sizing:border-box}' + BUTTON_CSS + INPUT_CSS +
             '</style>']
    places = []
    x = GAP
    for lib, name, w, h, bg, html in CELLS:
        parts.append('<div class="cell" style="left:%dpx;top:%dpx;width:%dpx;height:%dpx;'
                     'background:%s">%s</div>' % (x, GAP, w, h, bg, html))
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

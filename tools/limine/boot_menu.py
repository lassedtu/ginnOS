#!/usr/bin/env python3
"""Write limine.conf with a menu of screen sizes.

The base file (tools/limine/limine-<arch>.conf) has one boot entry. The
output has that entry first, at the size Limine picks (the monitor's own
size from its EDID when the firmware gives it, else the firmware's mode),
or at --resolution when one is given. A "Screen sizes" folder after it
has the same entry once for each size in --sizes, so a computer whose
firmware does not tell the monitor's size can still start in a good one.
Limine takes the nearest mode it has when a size is not offered.

usage: boot_menu.py BASE OUT [--resolution WxH] [--sizes "WxH WxH ..."]
                    [--timeout SECONDS]
"""

import argparse
import re
import sys

SIZE = re.compile(r"^[0-9]+x[0-9]+$")


def split(lines):
    """the lines before the first entry, the entry's title, its body"""
    for i, line in enumerate(lines):
        if line.startswith("/"):
            body = []
            for rest in lines[i + 1:]:
                if rest.startswith("/"):
                    break
                body.append(rest)
            while body and not body[-1].strip():
                body.pop()
            return lines[:i], line[1:].strip(), body
    sys.exit("boot_menu.py: the base file has no boot entry")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base")
    parser.add_argument("out")
    parser.add_argument("--resolution", default="")
    parser.add_argument("--sizes", default="")
    parser.add_argument("--timeout", default="")
    args = parser.parse_args()

    sizes = args.sizes.split()
    for size in sizes + ([args.resolution] if args.resolution else []):
        if not SIZE.match(size):
            sys.exit(f"boot_menu.py: not a size: {size} (use WIDTHxHEIGHT)")

    with open(args.base) as f:
        head, title, body = split(f.read().splitlines())
    if args.timeout:
        head = [f"timeout: {args.timeout}" if l.startswith("timeout:") else l for l in head]

    out = list(head)
    out.append(f"/{title}")
    out.extend(body)
    if args.resolution:
        out.append(f"    resolution: {args.resolution}")
    if sizes:
        out.append("")
        out.append("# the same, at a size chosen here")
        out.append("/Screen sizes")
        for size in sizes:
            out.append(f"//{title} at {size.replace('x', ' x ')}")
            out.extend(body)
            out.append(f"    resolution: {size}")
    with open(args.out, "w") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()

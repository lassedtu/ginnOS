#!/usr/bin/env python3
"""Make the reference pages of the docs from the comments in the source.

The script reads the doc comments (/** ... */) of the public headers, the
system call table and the ioctl numbers, and writes Markdown pages into
docs/reference/. Do not edit those pages: change the comments, then run
"make docs".

usage: make_reference.py [--out DIR]
"""

import argparse
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPO = "https://github.com/lassedtu/ginnOS/blob/main"
GENERATED = "<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->\n\n"

LIBC_INCLUDE = ROOT / "src/libc/include"
LIBRARIES = {
    "gfx": "drawing into pixel buffers",
    "heimdall": "windows on the desktop, and the window server protocol",
    "bragi": "TrueType text",
    "idunn": "the widget toolkit and the theme",
    "conf": "the settings files",
    "apps": "the .app files of the launcher",
    "mime": "file types and their icons",
    "icon": "the icon packs",
    "image": "PNG, JPEG and BMP files",
    "vt": "a terminal emulator without a screen",
}

COMMENT = re.compile(r"/\*\*((?:(?!\*/).)*)\*/", re.S)
TAG = re.compile(r"^@(\w+)\s*(.*)$")
IOCTL_NAME = re.compile(r"^#define\s+((?:[A-Z0-9]+_)*(?:IOC|IOCTL|TIOC|FBIO|SNDCTL|SIOC|TC)[A-Z0-9_]*)\s+(0x[0-9A-Fa-f]+u?|\d+)\s*(?://\s*(.*))?$")


def comment_lines(body):
    """Remove the " * " in front of each line of a comment body."""
    lines = body.split("\n")
    out = []
    for line in lines:
        line = line.rstrip()
        stripped = line.lstrip()
        if stripped.startswith("*"):
            stripped = stripped[1:]
            if stripped.startswith(" "):
                stripped = stripped[1:]
        out.append(stripped)
    while out and not out[0].strip():
        out.pop(0)
    while out and not out[-1].strip():
        out.pop()
    return out


def prose(text):
    """Escape the characters that Markdown reads as HTML or emphasis."""
    parts = re.split(r"(`[^`]*`)", text)
    for i in range(0, len(parts), 2):
        parts[i] = (parts[i].replace("&", "&amp;").replace("<", "&lt;")
                    .replace(">", "&gt;").replace("*", "\\*").replace("_", "\\_")
                    .replace("[", "\\["))
    return "".join(parts)


def cell(text):
    """Text for a table cell: prose, with the column separator escaped."""
    return prose(text).replace("|", "\\|")


def first_sentence(lines):
    """The first sentence of the first paragraph of a comment."""
    paragraph = []
    for line in lines:
        if not line.strip() or TAG.match(line.strip()) and not line.strip().startswith("@brief"):
            if paragraph:
                break
            continue
        paragraph.append(re.sub(r"^@brief\s*", "", line.strip()))
    text = " ".join(paragraph)
    match = re.match(r"(.+?\.)(\s|$)", text)
    sentence = match.group(1) if match else text
    return sentence.rstrip(".:;")


def render_comment(lines):
    """Turn the lines of a doc comment into Markdown.

    A run of indented lines after an empty line is code. @param lines
    become a list, and @return lines a paragraph.
    """
    out = []
    params = []
    returns = []
    current = None
    code = []
    in_code = False
    previous_blank = True

    def flush_code():
        nonlocal code
        if code:
            out.append("```c")
            indent = min(len(c) - len(c.lstrip()) for c in code if c.strip())
            out.extend(c[indent:] for c in code)
            out.append("```")
            out.append("")
            code = []

    for line in lines:
        tag = TAG.match(line.strip())
        if in_code:
            if line.startswith("  ") or not line.strip():
                code.append(line)
                continue
            while code and not code[-1].strip():
                code.pop()
            flush_code()
            in_code = False
        if tag:
            name, rest = tag.groups()
            if name == "param":
                words = rest.split(None, 1)
                params.append([words[0] if words else "", words[1] if len(words) > 1 else ""])
                current = params[-1]
            elif name in ("return", "returns"):
                returns.append(["", rest])
                current = returns[-1]
            elif name in ("brief", "note"):
                out.append(prose(rest))
                current = None
            else:
                current = None
            previous_blank = False
            continue
        if current is not None and line.strip() and line.startswith(" "):
            current[1] += " " + line.strip()
            continue
        current = None
        if previous_blank and line.startswith("  ") and line.strip():
            in_code = True
            code.append(line)
            continue
        out.append(prose(line))
        previous_blank = not line.strip()
    if in_code:
        while code and not code[-1].strip():
            code.pop()
        flush_code()
    text = "\n".join(out).strip()
    blocks = [text] if text else []
    if params:
        blocks.append("\n".join(f"- `{p}`: {prose(d)}" for p, d in params))
    for _, d in returns:
        blocks.append(f"Returns: {prose(d)}")
    return "\n\n".join(blocks)


def take_declaration(lines, start):
    """Read the declaration that starts at lines[start].

    Return the text and the index of the line after it. A function body
    is not taken: its signature ends with a semicolon.
    """
    i = start
    while i < len(lines) and not lines[i].strip():
        i += 1
    if i >= len(lines):
        return "", i
    if lines[i].lstrip().startswith("#"):
        taken = [lines[i]]
        while taken[-1].rstrip().endswith("\\") and i + 1 < len(lines):
            i += 1
            taken.append(lines[i])
        return "\n".join(taken), i + 1
    taken = []
    depth = 0
    while i < len(lines):
        line = lines[i]
        code = re.sub(r"//.*$", "", line)
        if depth == 0 and "{" in code:
            before = code.split("{", 1)[0]
            head = "\n".join(taken + [before]).strip()
            if head.endswith(")"):
                skip = i
                body_depth = 0
                while skip < len(lines):
                    body_depth += lines[skip].count("{") - lines[skip].count("}")
                    if body_depth <= 0 and "}" in lines[skip]:
                        break
                    skip += 1
                return head + ";", skip + 1
        taken.append(line)
        depth += code.count("{") - code.count("}")
        i += 1
        if depth <= 0 and code.rstrip().endswith(";"):
            break
    return "\n".join(taken), i


def declaration_name(text):
    """The name of a declaration, for its heading."""
    first = text.strip()
    match = re.match(r"#\s*define\s+(\w+)", first)
    if match:
        return match.group(1)
    flat = re.sub(r"//.*", "", text)
    flat = re.sub(r"/\*.*?\*/", "", flat, flags=re.S)
    flat = " ".join(flat.split())
    match = re.search(r"\}\s*(\w+)\s*;\s*$", flat)
    if match:
        return match.group(1)
    match = re.search(r"\(\s*\*\s*(\w+)\s*\)\s*\(", flat)
    if match:
        return match.group(1)
    match = re.search(r"(\w+)\s*\(", flat)
    if match:
        return match.group(1)
    match = re.search(r"^(?:typedef\s+)?(?:struct|enum|union)\s+(\w+)", flat)
    if match:
        return match.group(1)
    match = re.search(r"(\w+)\s*(?:\[[^\]]*\])?\s*;\s*$", flat)
    return match.group(1) if match else ""


def parse_header(path):
    """Read a header: its file comment and its documented declarations."""
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.split("\n")
    offsets = []
    position = 0
    for line in lines:
        offsets.append(position)
        position += len(line) + 1

    def line_of(offset):
        low, high = 0, len(offsets) - 1
        while low < high:
            middle = (low + high + 1) // 2
            if offsets[middle] <= offset:
                low = middle
            else:
                high = middle - 1
        return low

    intro = ""
    items = []
    for match in COMMENT.finditer(text):
        body = comment_lines(match.group(1))
        if any(line.strip().startswith("@file") for line in body):
            body = [line for line in body if not line.strip().startswith("@file")]
            intro = render_comment(body)
            continue
        end_line = line_of(match.end()) + 1
        if text[offsets[end_line - 1]:match.start()].strip() and not text[offsets[end_line - 1]:match.start()].strip().startswith("/"):
            continue
        declarations = []
        i = end_line
        while True:
            declaration, after = take_declaration(lines, i)
            if not declaration.strip() or declaration.lstrip().startswith("/"):
                break
            declarations.append(declaration.rstrip())
            i = after
            if i >= len(lines) or not lines[i].strip() or lines[i].lstrip().startswith(("/*", "//")):
                break
        if not declarations:
            continue
        names = [n for n in (declaration_name(d) for d in declarations) if n]
        if not names:
            continue
        items.append({"names": names, "code": "\n".join(declarations),
                      "doc": render_comment(body), "line": end_line + 1})
    return intro, items


def header_section(path, level, title):
    """The Markdown of one header: its intro, then each declaration."""
    intro, items = parse_header(path)
    relative = path.relative_to(ROOT).as_posix()
    out = [f"{'#' * level} {title}", "",
           f"Source: [`{relative}`]({REPO}/{relative})", ""]
    if intro:
        out += [intro, ""]
    if not items:
        out += ["This header has no doc comments yet.", ""]
    seen = {}
    for item in items:
        name = ", ".join(dict.fromkeys(item["names"]))
        heading = name
        if heading in seen:
            seen[heading] += 1
            heading = f"{heading} ({seen[heading]})"
        else:
            seen[heading] = 1
        out += [f"{'#' * (level + 1)} {prose(heading)}", "",
                "```c", item["code"], "```", ""]
        if item["doc"]:
            out += [item["doc"], ""]
    return "\n".join(out)


def file_comment(path):
    """The lines of the @file comment of a header, or an empty list."""
    for match in COMMENT.finditer(path.read_text(encoding="utf-8", errors="replace")):
        body = comment_lines(match.group(1))
        if any(line.strip().startswith("@file") for line in body):
            return [line for line in body if not line.strip().startswith("@file")]
    return []


def libc_page_name(header):
    return header.as_posix().removesuffix(".h").replace("/", "-")


def make_libc(out_dir):
    headers = sorted(p.relative_to(LIBC_INCLUDE) for p in LIBC_INCLUDE.rglob("*.h"))
    (out_dir / "libc").mkdir(parents=True)
    rows = []
    for header in headers:
        name = libc_page_name(header)
        intro_lines = file_comment(LIBC_INCLUDE / header)
        _, items = parse_header(LIBC_INCLUDE / header)
        summary = cell(first_sentence(intro_lines))
        rows.append(f"| [`<{header.as_posix()}>`]({name}.md) | {len(items)} | {summary} |")
        page = header_section(LIBC_INCLUDE / header, 1, f"&lt;{header.as_posix()}&gt;")
        (out_dir / "libc" / f"{name}.md").write_text(GENERATED + page, encoding="utf-8")
    index = [
        "# libc", "",
        "The headers of the C library, with the doc comment of each",
        "declaration. For what libc has and does not have, see",
        "[libc](../../develop/libc.md).", "",
        "| Header | Documented | What it is |", "| --- | --- | --- |", *rows, "",
    ]
    (out_dir / "libc" / "README.md").write_text(GENERATED + "\n".join(index), encoding="utf-8")


def make_libraries(out_dir):
    rows = []
    for name, what in LIBRARIES.items():
        include = ROOT / "src/lib" / name / "include"
        headers = sorted(include.glob("*.h"))
        sections = [header_section(h, 2, f"&lt;{h.name}&gt;") for h in headers]
        page = [f"# {name}", "", f"lib{name}: {what}.", "", *sections]
        (out_dir / f"{name}.md").write_text(GENERATED + "\n".join(page), encoding="utf-8")
        rows.append(f"| [{name}]({name}.md) | {what} |")
    return rows


def syscall_docs():
    """The comment above each sys_ function, by its name."""
    docs = {}
    for source in sorted((ROOT / "src/kernel").rglob("*.c")):
        text = source.read_text(encoding="utf-8", errors="replace")
        for match in re.finditer(r"/\*\*((?:(?!\*/).)*)\*/\s*(?:static\s+)?syscall_ret_t\s+(sys_\w+)\s*\(", text, re.S):
            docs[match.group(2)] = (comment_lines(match.group(1)), source.relative_to(ROOT).as_posix())
    return docs


def make_syscalls(out_dir):
    header = ROOT / "src/kernel/syscall/syscall.h"
    numbers = []
    for line in header.read_text(encoding="utf-8").split("\n"):
        match = re.match(r"#define\s+SYS_(\w+)\s+(\d+)\s*(?://\s*(.*))?$", line)
        if match:
            numbers.append((int(match.group(2)), match.group(1), match.group(3) or ""))
    dispatch = (ROOT / "src/kernel/syscall/syscall_dispatch.c").read_text(encoding="utf-8")
    handlers = dict(re.findall(r"\[SYS_(\w+)\]\s*=\s*(sys_\w+)", dispatch))
    docs = syscall_docs()
    rows = []
    sections = []
    for number, name, note in sorted(numbers):
        handler = handlers.get(name)
        lower = name.lower()
        if not handler:
            rows.append(f"| {number} | `{lower}` | {cell(note) or 'not used'} |")
            continue
        lines, source = docs.get(handler, ([], ""))
        lines = [re.sub(r"^SYS_\w+:\s*", "", l, count=1) if i == 0 else l for i, l in enumerate(lines)]
        summary = first_sentence(lines)
        rows.append(f"| {number} | [`{lower}`](#{lower}) | {cell(summary)} |")
        sections += [f"## {lower}", "", f"Number {number}. Handler: `{handler}`"
                     + (f" in [`{source}`]({REPO}/{source})." if source else "."), ""]
        if lines:
            sections += [render_comment(lines), ""]
    page = [
        "# The system calls", "",
        "A program puts the number in EAX (RAX on x86_64) and the arguments",
        "in EBX, ECX, EDX, ESI and EDI, then runs `int 0x80` (i686) or",
        "`syscall` (x86_64). A negative result is an error number. The numbers",
        f"are in [`src/kernel/syscall/syscall.h`]({REPO}/src/kernel/syscall/syscall.h).", "",
        "| Number | Name | What it does |", "| --- | --- | --- |", *rows, "", *sections,
    ]
    (out_dir / "syscalls.md").write_text(GENERATED + "\n".join(page), encoding="utf-8")


def make_ioctls(out_dir):
    out = [
        "# The ioctls", "",
        "The requests of `ioctl` on devices and sockets, from the libc headers.", "",
    ]
    for path in sorted(LIBC_INCLUDE.rglob("*.h")):
        rows = []
        lines = path.read_text(encoding="utf-8").split("\n")
        for i, line in enumerate(lines):
            match = IOCTL_NAME.match(line.strip())
            if not match or match.group(1).startswith(("SYS_", "TCSA", "TCOO", "TCIO", "TCIF", "TCOF")):
                continue
            note = match.group(3) or ""
            if not note:
                j = i - 1
                above = []
                while j >= 0 and lines[j].strip().startswith("//"):
                    above.insert(0, lines[j].strip()[2:].strip())
                    j -= 1
                note = " ".join(above)
            rows.append(f"| `{match.group(1)}` | `{match.group(2)}` | {cell(note)} |")
        if rows:
            header = path.relative_to(LIBC_INCLUDE).as_posix()
            page = libc_page_name(path.relative_to(LIBC_INCLUDE))
            out += [f"## &lt;{header}&gt;", "",
                    f"See [`<{header}>`](libc/{page}.md).", "",
                    "| Request | Number | What it does |", "| --- | --- | --- |", *rows, ""]
    (out_dir / "ioctls.md").write_text(GENERATED + "\n".join(out), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", default=str(ROOT / "docs/reference"))
    args = parser.parse_args()
    out_dir = Path(args.out)
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)
    make_libc(out_dir)
    library_rows = make_libraries(out_dir)
    make_syscalls(out_dir)
    make_ioctls(out_dir)
    index = [
        "# Reference", "",
        "These pages come from the comments in the source. To change a page,",
        "change the comment, then run `make docs`.", "",
        "- [The system calls](syscalls.md)",
        "- [The ioctls](ioctls.md)",
        "- [libc](libc/README.md): a page for each header", "",
        "| Library | What it is |", "| --- | --- |", *library_rows, "",
    ]
    (out_dir / "README.md").write_text(GENERATED + "\n".join(index), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())

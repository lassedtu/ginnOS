#!/usr/bin/env python3
"""Create an ext2 image from a source directory.

Requires `mke2fs` (or `mkfs.ext2`) from e2fsprogs.
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MIB = 1024 * 1024
IGNORED_NAMES = {".DS_Store"}
PERMISSIONS_FILE = Path(__file__).with_name("permissions")
S_IFREG = 0o100000
S_IFDIR = 0o040000


def find_mke2fs() -> str:
    """Locate an ext2 filesystem creation binary."""
    candidates = [
        shutil.which("mke2fs"),
        shutil.which("mkfs.ext2"),
        "/opt/homebrew/opt/e2fsprogs/sbin/mke2fs",
        "/usr/local/opt/e2fsprogs/sbin/mke2fs",
    ]

    for candidate in candidates:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

    raise FileNotFoundError(
        "mke2fs not found. Install e2fsprogs (macOS: `brew install e2fsprogs`)."
    )


def source_size_bytes(source_dir: Path) -> int:
    """Return total size of regular files in directory tree."""
    total = 0
    for root, _, files in os.walk(source_dir):
        for name in files:
            if name in IGNORED_NAMES:
                continue
            path = Path(root) / name
            if path.is_symlink():
                continue  # a link takes no data blocks (its target is short)
            try:
                stat = path.stat()
            except OSError:
                continue
            if path.is_file():
                total += stat.st_size
    return total


def ignore_unwanted(_dir: str, names: list[str]) -> set[str]:
    """Return names that should be excluded from the generated image."""
    return {name for name in names if name in IGNORED_NAMES}


def read_permissions(path: Path) -> list[tuple[str, bool, int, int, int]]:
    """the lines of the permissions table: (path, folder, mode, uid, gid)."""
    entries = []
    if not path.exists():
        return entries
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        name, mode, uid, gid = line.split()
        folder = mode.startswith("d")
        entries.append((name, folder, int(mode.lstrip("d"), 8), int(uid), int(gid)))
    return entries


def prepare_filtered_source(source_dir: Path, temp_root: Path) -> Path:
    """Copy source tree to a temp location while excluding unwanted files.

    Symbolic links stay links (mke2fs -d makes them links in the image).
    """
    filtered_dir = temp_root / "rootfs"
    shutil.copytree(source_dir, filtered_dir, ignore=ignore_unwanted, symlinks=True)
    # the folders of the permissions table that are not in the tree
    for name, folder, _mode, _uid, _gid in read_permissions(PERMISSIONS_FILE):
        if folder:
            (filtered_dir / name.lstrip("/")).mkdir(parents=True, exist_ok=True)
    return filtered_dir


def recommend_size_mb(source_dir: Path, min_size_mb: int) -> int:
    """
    Estimate ext2 image size.

    - 50% metadata/slack overhead
    - 2 MiB growth space
    - round up
    """

    data_bytes = source_size_bytes(source_dir)

    estimated_bytes = int(data_bytes * 1.5) + (2 * MIB)

    estimated_mb = max(
        1,
        math.ceil(estimated_bytes / MIB)
    )

    return max(min_size_mb, estimated_mb)


def find_debugfs(mke2fs_bin: str) -> str:
    """debugfs comes with mke2fs in e2fsprogs: look next to it first."""
    beside = Path(mke2fs_bin).with_name("debugfs")
    if beside.exists():
        return str(beside)
    found = shutil.which("debugfs")
    if found:
        return found
    raise FileNotFoundError("debugfs not found (it comes with e2fsprogs)")


def set_root_owner(mke2fs_bin: str, source_dir: Path, image: Path) -> int:
    """set uid and gid 0 on every file and directory of the image."""
    paths = ["/"]
    for entry in sorted(source_dir.rglob("*")):
        paths.append("/" + entry.relative_to(source_dir).as_posix())

    commands = []
    for path in paths:
        commands.append(f'set_inode_field "{path}" uid 0')
        commands.append(f'set_inode_field "{path}" gid 0')
    # then the owners and modes of the permissions table
    for name, folder, mode, uid, gid in read_permissions(PERMISSIONS_FILE):
        if not (source_dir / name.lstrip("/")).exists():
            continue  # a program that this build does not have
        kind = S_IFDIR if folder or (source_dir / name.lstrip("/")).is_dir() else S_IFREG
        commands.append(f'set_inode_field "{name}" mode 0{kind | mode:o}')
        commands.append(f'set_inode_field "{name}" uid {uid}')
        commands.append(f'set_inode_field "{name}" gid {gid}')

    try:
        debugfs_bin = find_debugfs(mke2fs_bin)
    except FileNotFoundError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    with tempfile.NamedTemporaryFile("w", suffix=".cmds", delete=False) as script:
        script.write("\n".join(commands) + "\n")
        script_path = script.name
    try:
        result = subprocess.run(
            [debugfs_bin, "-w", "-f", script_path, str(image)],
            capture_output=True,
            text=True,
        )
    finally:
        Path(script_path).unlink()

    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
    return result.returncode


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create an ext2 image from a source folder."
    )
    parser.add_argument(
        "--source",
        required=True,
        help="Directory whose contents will become the ext2 root.",
    )
    parser.add_argument(
        "--output",
        required=True,
        help="Output image path.",
    )
    parser.add_argument(
        "--size-mb",
        type=int,
        default=None,
        help="Image size in MiB. If omitted, a recommended size is used.",
    )
    parser.add_argument(
        "--min-size-mb",
        type=int,
        default=4,
        help="Minimum size when auto-sizing is used (default: 4).",
    )
    parser.add_argument(
        "--label",
        default="GINNOS_EXT2",
        help="Filesystem label (default: GINNOS_EXT2).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    source_dir = Path(args.source).expanduser().resolve()
    output_image = Path(args.output).expanduser().resolve()

    if not source_dir.exists() or not source_dir.is_dir():
        print(f"Error: source directory does not exist: {source_dir}", file=sys.stderr)
        return 1

    if args.size_mb is not None and args.size_mb <= 0:
        print("Error: --size-mb must be a positive integer", file=sys.stderr)
        return 1

    if args.min_size_mb <= 0:
        print("Error: --min-size-mb must be a positive integer", file=sys.stderr)
        return 1

    try:
        mke2fs_bin = find_mke2fs()
    except FileNotFoundError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    size_mb = args.size_mb
    if size_mb is None:
        size_mb = recommend_size_mb(source_dir, args.min_size_mb)

    output_image.parent.mkdir(parents=True, exist_ok=True)

    with output_image.open("wb") as image_file:
        image_file.truncate(size_mb * MIB)

    with tempfile.TemporaryDirectory(prefix="ginnos-ext2-") as temp_dir:
        filtered_source = prepare_filtered_source(source_dir, Path(temp_dir))

        cmd = [
            mke2fs_bin,
            "-q",
            "-t",
            "ext2",
            "-F",
            "-L",
            args.label,
            "-d",
            str(filtered_source),
            str(output_image),
        ]

        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            if result.stdout:
                print(result.stdout, file=sys.stderr)
            if result.stderr:
                print(result.stderr, file=sys.stderr)
            return result.returncode

        # mke2fs -d copies the build user's uid and gid; on ginnOS the
        # system files belong to root, so set every inode to 0:0
        status = set_root_owner(mke2fs_bin, filtered_source, output_image)
        if status != 0:
            return status

    print(f"Created ext2 image: {output_image}")
    print(f"Source directory: {source_dir}")
    print(f"Image size: {size_mb} MiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

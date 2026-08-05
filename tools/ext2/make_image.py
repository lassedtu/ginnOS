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


def prepare_filtered_source(source_dir: Path, temp_root: Path) -> Path:
    """Copy source tree to a temp location while excluding unwanted files."""
    filtered_dir = temp_root / "rootfs"
    shutil.copytree(source_dir, filtered_dir, ignore=ignore_unwanted)
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

    print(f"Created ext2 image: {output_image}")
    print(f"Source directory: {source_dir}")
    print(f"Image size: {size_mb} MiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

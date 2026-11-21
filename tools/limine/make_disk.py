#!/usr/bin/env python3
"""Make a disk image that limine boots on BIOS and UEFI machines.

The disk has an MBR partition table (or, with --gpt, a GPT) with two
partitions:
  1. a FAT boot partition (type 0xEF, the EFI system partition) with limine,
     limine.conf, the kernel and the root filesystem image (a boot module);
  2. the ext2 root filesystem (type 0x83).
The BIOS part of limine goes into the MBR and the gap before partition 1
(`limine bios-install`).

The FAT partition is filled with mtools, so the script needs no root access
and no loop devices.

usage:
  make_disk.py --output disk.img --ext2 rootfs.ext2 --limine-dir DIR \
               --file SRC:DEST [--file SRC:DEST ...] [--fat-size-mb N]
"""

import argparse
import shutil
import uuid
import zlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

SECTOR = 512
MIB = 1024 * 1024
ALIGN_SECTORS = MIB // SECTOR  # partitions start on 1 MiB boundaries

TYPE_EFI_SYSTEM = 0xEF
TYPE_LINUX = 0x83
TYPE_GPT_PROTECTIVE = 0xEE

GUID_EFI_SYSTEM = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
GUID_LINUX_FS = uuid.UUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4")
# where `limine bios-install` puts its BIOS stage on a GPT disk
GUID_BIOS_BOOT = uuid.UUID("21686148-6449-6E6F-744E-656564454649")
GPT_ENTRIES = 128
GPT_ENTRY_SIZE = 128
GPT_ARRAY_SECTORS = GPT_ENTRIES * GPT_ENTRY_SIZE // SECTOR


def run(cmd: list) -> None:
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(f"command failed: {' '.join(cmd)}")


def need_tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise SystemExit(f"{name} not found. Install mtools (macOS: `brew install mtools`).")
    return path


def make_fat(path: Path, size_mb: int, files: list) -> None:
    """Make a FAT image and copy the files into it (DEST paths are absolute)."""
    mformat = need_tool("mformat")
    mmd = need_tool("mmd")
    mcopy = need_tool("mcopy")

    with open(path, "wb") as image:
        image.truncate(size_mb * MIB)
    run([mformat, "-i", str(path), "-v", "GINNOS_BOOT", "::"])

    made = set()
    for src, dest in files:
        parts = Path(dest).parts[1:-1]  # the directories under the root
        for depth in range(1, len(parts) + 1):
            directory = "/" + "/".join(parts[:depth])
            if directory not in made:
                run([mmd, "-i", str(path), "::" + directory])
                made.add(directory)
        run([mcopy, "-i", str(path), src, "::" + dest])


def partition_entry(bootable: bool, ptype: int, start: int, count: int) -> bytes:
    """An MBR partition entry that uses LBA only (the CHS fields say so)."""
    chs_lba_only = bytes([0xFE, 0xFF, 0xFF])
    return (
        bytes([0x80 if bootable else 0x00])
        + chs_lba_only
        + bytes([ptype])
        + chs_lba_only
        + struct.pack("<II", start, count)
    )


def gpt_entry(type_guid: uuid.UUID, first: int, last: int, name: str) -> bytes:
    """One GPT partition entry (GUIDs are stored mixed-endian)."""
    return (
        type_guid.bytes_le
        + uuid.uuid4().bytes_le
        + struct.pack("<QQQ", first, last, 0)
        + name.encode("utf-16-le").ljust(72, b"\0")
    )


def gpt_header(current: int, backup: int, first_usable: int, last_usable: int,
               disk_guid: uuid.UUID, array_lba: int, array_crc: int) -> bytes:
    """A GPT header sector; the CRC covers the 92-byte header."""
    fields = [b"EFI PART", struct.pack("<II", 0x00010000, 92), b"\0" * 4,
              struct.pack("<I", 0), struct.pack("<QQQQ", current, backup, first_usable, last_usable),
              disk_guid.bytes_le, struct.pack("<QII", array_lba, GPT_ENTRIES, GPT_ENTRY_SIZE),
              struct.pack("<I", array_crc)]
    header = bytearray(b"".join(fields))
    struct.pack_into("<I", header, 16, zlib.crc32(bytes(header)))
    return bytes(header).ljust(SECTOR, b"\0")


def write_gpt(disk, total_sectors: int, parts: list) -> None:
    """A protective MBR, the primary GPT at LBA 1-33 and the backup at the end."""
    array = b"".join(gpt_entry(*p) for p in parts).ljust(GPT_ENTRIES * GPT_ENTRY_SIZE, b"\0")
    array_crc = zlib.crc32(array)
    disk_guid = uuid.uuid4()
    last = total_sectors - 1
    first_usable = 1 + 1 + GPT_ARRAY_SECTORS
    last_usable = last - GPT_ARRAY_SECTORS - 1

    mbr = bytearray(SECTOR)
    entry = partition_entry(False, TYPE_GPT_PROTECTIVE, 1, min(total_sectors - 1, 0xFFFFFFFF))
    mbr[0x1BE:0x1BE + 16] = entry
    mbr[510] = 0x55
    mbr[511] = 0xAA
    disk.seek(0)
    disk.write(mbr)

    disk.seek(SECTOR)
    disk.write(gpt_header(1, last, first_usable, last_usable, disk_guid, 2, array_crc))
    disk.write(array)
    disk.seek((last - GPT_ARRAY_SECTORS) * SECTOR)
    disk.write(array)
    disk.write(gpt_header(last, 1, first_usable, last_usable, disk_guid,
                          last - GPT_ARRAY_SECTORS, array_crc))


def align_up(sectors: int) -> int:
    return (sectors + ALIGN_SECTORS - 1) // ALIGN_SECTORS * ALIGN_SECTORS


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", required=True)
    parser.add_argument("--ext2", required=True, help="the root filesystem image")
    parser.add_argument("--limine-dir", required=True, help="the fetched limine release")
    parser.add_argument("--file", action="append", default=[], help="SRC:DEST in the FAT")
    parser.add_argument("--gpt", action="store_true",
                        help="a GPT disk (for UEFI machines) instead of an MBR disk")
    parser.add_argument("--fat-size-mb", type=int, default=0,
                        help="FAT size (default: the files plus 4 MiB, at least 16 MiB)")
    args = parser.parse_args()

    files = []
    for item in args.file:
        src, sep, dest = item.partition(":")
        if not sep or not dest.startswith("/"):
            raise SystemExit(f"bad --file {item}: use SRC:/DEST")
        files.append((src, dest))

    ext2 = Path(args.ext2)
    limine_tool = Path(args.limine_dir) / "limine"
    if not limine_tool.exists():
        raise SystemExit(f"{limine_tool} not found: run tools/limine/fetch.sh first")

    fat_size_mb = args.fat_size_mb
    if fat_size_mb == 0:
        total = sum(Path(src).stat().st_size for src, _ in files)
        fat_size_mb = max(16, total // MIB + 4)

    output = Path(args.output)
    with tempfile.TemporaryDirectory(prefix="ginnos-disk-") as temp:
        fat = Path(temp) / "boot.fat"
        make_fat(fat, fat_size_mb, files)

        fat_start = ALIGN_SECTORS
        fat_count = fat.stat().st_size // SECTOR
        ext2_start = align_up(fat_start + fat_count)
        ext2_count = (ext2.stat().st_size + SECTOR - 1) // SECTOR
        total_sectors = ext2_start + ext2_count
        if args.gpt:
            total_sectors += 1 + GPT_ARRAY_SECTORS  # the backup GPT

        tmp_output = output.with_suffix(output.suffix + ".tmp")
        with open(tmp_output, "wb") as disk:
            disk.truncate(total_sectors * SECTOR)

            if args.gpt:
                write_gpt(disk, total_sectors, [
                    (GUID_EFI_SYSTEM, fat_start, fat_start + fat_count - 1, "GINNOS_BOOT"),
                    (GUID_LINUX_FS, ext2_start, ext2_start + ext2_count - 1, "GINNOS_ROOT"),
                    # the gap before the first partition, listed last so the
                    # boot and root partitions stay numbers 1 and 2
                    (GUID_BIOS_BOOT, 2 + GPT_ARRAY_SECTORS, fat_start - 1, "BIOS_BOOT"),
                ])
            else:
                mbr = bytearray(SECTOR)
                mbr[0x1B8:0x1BC] = struct.pack("<I", 0x67696E6E)  # disk signature "ginn"
                entries = partition_entry(True, TYPE_EFI_SYSTEM, fat_start, fat_count)
                entries += partition_entry(False, TYPE_LINUX, ext2_start, ext2_count)
                mbr[0x1BE:0x1BE + len(entries)] = entries
                mbr[510] = 0x55
                mbr[511] = 0xAA
                disk.seek(0)
                disk.write(mbr)

            disk.seek(fat_start * SECTOR)
            disk.write(fat.read_bytes())
            disk.seek(ext2_start * SECTOR)
            disk.write(ext2.read_bytes())

        # the BIOS boot code: the MBR code and the stage in the gap before
        # partition 1. the partition table stays.
        run([str(limine_tool), "bios-install", "--quiet", str(tmp_output)])
        tmp_output.replace(output)

    return 0


if __name__ == "__main__":
    sys.exit(main())

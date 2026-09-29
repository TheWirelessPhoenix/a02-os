#!/usr/bin/env python3
"""Locate the LFI firmware inside a raw NAND dump produced by `read_nand`.

The ATJ2157 A02 stores main data UNSRAMBLED, so the LFI (the stock firmware
container) can be found directly by its magic. `find_lfi` in actions_dump fails
only because it hardcodes a spare-tag format this chip does not use.

LFI format (see tools/actions_flash/fwhelper/main.c):
    0x000  magic 0x0ff0aa55
    0x004  version string, e.g. "1.101.37"
    0x010  directory checksum (32-bit) over 0x200..0x1e00
    0x1fe  header checksum (16-bit) over 0x000..0x1fe
    0x200  directory: entries of 0x20 bytes, up to 0x2000
           +0x00 8.3 name (11 bytes, space padded)
           +0x10 file offset in 512-byte sectors
           +0x14 file length
           +0x1c file checksum (32-bit)
    then file data follows, addressed by the directory.

Usage:
  python3 lfi_scan.py dump.bin --page-size 8192 [--blocks N] [--list]
"""
import argparse
import struct
import sys
from pathlib import Path

MAGIC = 0x0FF0AA55


def checksum16(b: bytes) -> int:
    # ATJ fw_checksum16: sum of little-endian 16-bit words (see fwhelper/main.c).
    return sum(struct.unpack_from("<%dH" % (len(b) // 2), b, 0)) & 0xFFFF


def checksum32(b: bytes) -> int:
    return sum(struct.unpack_from("<%dI" % (len(b) // 4), b, 0)) & 0xFFFFFFFF


def find_headers(data: bytes, page_size: int):
    """Yield (offset, page, offset_in_page, version) for every LFI header."""
    needle = struct.pack("<I", MAGIC)
    i = 0
    while True:
        i = data.find(needle, i)
        if i < 0:
            return
        ver = data[i + 4:i + 16].split(b"\0")[0].decode("latin1", "replace")
        yield i, i // page_size, i % page_size, ver
        i += 1


def parse_dir(data: bytes, header_off: int):
    """Parse the LFI directory. Returns (files, notes)."""
    files = []
    base = header_off
    if base + 0x2000 > len(data):
        return files, ["directory truncated"]
    for i in range(0x200, 0x2000, 0x20):
        ent = data[base + i:base + i + 0x20]
        if not ent or ent[0] == 0:
            break
        raw = ent[0:11]
        name8 = raw[0:8].decode("latin1", "replace").rstrip(" ")
        ext = raw[8:11].decode("latin1", "replace").rstrip(" ")
        name = f"{name8}.{ext}" if ext else name8
        off = struct.unpack_from("<I", ent, 0x10)[0] << 9
        length = struct.unpack_from("<I", ent, 0x14)[0]
        chk = struct.unpack_from("<I", ent, 0x1C)[0]
        files.append({"name": name, "offset": off, "length": length, "checksum": chk})
    return files, []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--page-size", type=lambda x: int(x, 0), default=8192)
    ap.add_argument("--blocks", type=lambda x: int(x, 0), default=None,
                    help="pages per block, to name blocks in output")
    ap.add_argument("--list", action="store_true", help="print the LFI directory")
    a = ap.parse_args()

    data = Path(a.dump).read_bytes()
    headers = list(find_headers(data, a.page_size))
    print(f"dump: {a.dump} ({len(data)} bytes)")
    print(f"LFI headers found: {len(headers)}")
    for off, page, op, ver in headers[:60]:
        blk = f"blk {page // a.blocks}" if a.blocks else ""
        print(f"  0x{off:08x}  page {page:5}  +0x{op:03x}  ver={ver!r}  {blk}")
    if len(headers) > 60:
        print(f"  ... and {len(headers) - 60} more")

    # First header at offset-in-page 0 is the true LFI start.
    starts = [h for h in headers if h[2] == 0]
    if starts:
        off = starts[0][0]
        files, notes = parse_dir(data, off)
        print(f"\nLFI start @0x{off:x} ({len(files)} directory entries)")
        for n in notes:
            print("  note:", n)
        if a.list:
            for f in files:
                print(f"  {f['name']:<12} off=0x{f['offset']:x} len=0x{f['length']:x}")
    else:
        print("\nNo LFI header at a page boundary in this dump "
              "(the LFI start may be in a block not included yet).")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Build the firmware image (LFI) that boots A02-OS from flash, and check it like the boot chain will.

  stock LFI  +  KER_INIT.BIN := os/a02os/boot/stub.bin  +  A02OS.BIN := os/a02os/a02os.bin

Checks (fail-closed):
  - every LFI checksum (header, directory, each file) — BREC's check_lfi
  - KER_TEXT / KER_DATA / KER_INIT still in the first directory sector (BREC reads only that one)
  - fits the firmware area: <= 0x20000 sectors per copy (capacity info in this unit's BREC)
  - replays the stub: finds A02OS.BIN in the directory, size/alignment limits, checksum
  - stub: entry code at +0, original KER_INIT byte-identical at +0x800

  mk_a02os_lfi.py -o image.lfi [--upgrade-hex UPGRADE.HEX]
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import a02fw  # noqa: E402
import paths  # noqa: E402
REPO = paths.REPO
OS_DIR = REPO / "os/a02os" if (REPO / "os/a02os").exists() else REPO / "os"

LFI_CAP_SECTORS = 0x20000   # (logical 0x07cc0000 - udisk 0x07c78000 - vm 0x8000) / 2 copies
# BREC's check_lfi (0x118AE8) requires 16 + sum(file sectors) == BREC header +8 (0x1C1FC here):
# "lfi_size not match!". So the image must stay exactly the stock size. Room for A02-OS comes
# from language tables English never uses, shrunk in place (same name, same position).
DONORS = ["874L.TBL", "1253L.TBL", "1255L.TBL", "1257L.TBL"]   # Thai, Greek, Hebrew, Baltic


def secs(n):
    return (n + 511) // 512


def make_room(db, replace, add):
    """Shrink donor files so the image keeps the stock total. Returns the replace dict."""
    stock = {n: b for n, b in db.files("FWIM")}
    grow = sum(secs(len(b)) - secs(len(stock[n])) for n, b in replace.items()) + \
        sum(secs(len(b)) for _, b in add)
    out = dict(replace)
    for d in DONORS:
        if grow <= 0:
            break
        have = secs(len(stock[d]))
        take = min(grow, have - 1)                  # keep at least one sector
        out[d] = stock[d][:(have - take) * 512]
        grow -= take
    if grow > 0:
        raise ValueError("not enough donor space for %d more sectors" % grow)
    if grow < 0:
        raise ValueError("internal: overshoot")
    return out


def check(lfi, stub, a02os, orig_ker_init):
    d = {n.strip(): (sec, ln, ck) for n, sec, ln, ck in
         [(e[0][:8].strip() + "." + e[0][8:].strip(), e[1], e[2], e[3]) for e in a02fw.parse_lfi(lfi)]}
    first = [lfi[i:i + 11] for i in range(0x200, 0x400, 0x20)]
    for k in (b"KER_TEXTBIN", b"KER_DATABIN", b"KER_INITBIN"):
        if k not in first:
            raise ValueError("%s not in the first directory sector" % k)
    if len(lfi) // 512 > LFI_CAP_SECTORS:
        raise ValueError("image larger than the firmware area")
    sec, ln, ck = d["KER_INIT.BIN"]
    if lfi[sec * 512: sec * 512 + len(stub)] != stub:
        raise ValueError("KER_INIT is not the stub")
    if stub[0x800:0x800 + len(orig_ker_init)] != orig_ker_init:
        raise ValueError("stub does not carry the original KER_INIT")
    # replay stub.c load_a02os()
    hits = [i for i in range(0x200, 0x2000, 0x20) if lfi[i:i + 11] == b"A02OS   BIN"]
    if len(hits) != 1:
        raise ValueError("A02OS.BIN missing or duplicated")
    sec, ln, ck = struct.unpack_from("<II", lfi, hits[0] + 0x10) + (struct.unpack_from("<I", lfi, hits[0] + 0x1c)[0],)
    if not ln or ln % 512 or ln > 0x10000:
        raise ValueError("A02OS.BIN size %d outside the stub's limits" % ln)
    body = lfi[sec * 512: sec * 512 + ln]
    if a02fw.sum32(body) != ck or body[:len(a02os)] != a02os:
        raise ValueError("A02OS.BIN contents/checksum")
    if 0x120000 + ln > 0x130000:
        raise ValueError("A02-OS would overwrite the ADFU magic word")
    return len(d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--stub", default=str(OS_DIR / "boot/stub.bin"))
    ap.add_argument("--a02os", default=str(OS_DIR / "a02os.bin"))
    a = ap.parse_args()
    db = a02fw.FirmwareDB(paths.need(paths.stock_afi(), "firmware database", paths.FETCH_HINT))
    stub = Path(a.stub).read_bytes()
    a02os = Path(a.a02os).read_bytes()
    orig = dict(db.files("FWIM"))["KER_INIT.BIN"]
    add = [("A02OS.BIN", a02os)]
    try:
        replace = make_room(db, {"KER_INIT.BIN": stub}, add)
    except ValueError as e:
        sys.exit("REFUSED: %s" % e)
    lfi = a02fw.build_lfi(db, replace=replace, add=add)
    if len(lfi) != a02fw.STOCK_LFI_SIZE:
        sys.exit("REFUSED: image is %d bytes, BREC requires exactly %d" % (len(lfi), a02fw.STOCK_LFI_SIZE))
    for n in sorted(set(replace) - {"KER_INIT.BIN"}):
        print("  room: %s shrunk to %d bytes" % (n, len(replace[n])))
    try:
        n = check(lfi, stub, a02os, orig)
    except ValueError as e:
        sys.exit("REFUSED: %s" % e)
    Path(a.output).write_bytes(lfi)
    print("OK %s: %d files, %d sectors (%.1f%% of the firmware area), sha256 %s"
          % (a.output, n, len(lfi) // 512, 100.0 * len(lfi) / 512 / LFI_CAP_SECTORS, a02fw.sha256(lfi)))


if __name__ == "__main__":
    main()

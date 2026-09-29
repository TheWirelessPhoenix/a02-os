#!/usr/bin/env python3
"""Build (and verify) an UPGRADE.HEX for the AGPTEK A02's own on-device updater — no Windows.

  mkupgrade.py stock  -o UPGRADE.HEX            # official firmware, byte-identical to what ships
  mkupgrade.py build  -o UPGRADE.HEX --replace NAME=path ...   # stock + replaced/added files
  mkupgrade.py verify UPGRADE.HEX [--expect-stock]             # replay the updater's checks

Inputs are the official A02_20241019.fw (hash-pinned) and its decrypted database (.afi), both
fetched into the local vendor cache by tools/flash/fetch_firmware.py (never stored in this repo).
The result is verified before it is written: decrypted again with an independent code path,
LFI checksums replayed exactly as fwupdate.AP does, NAND ID looked up in FLASH_ID.BIN.
"""
import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import a02fw  # noqa: E402
import paths  # noqa: E402

DEFAULT_FW = paths.stock_fw()
DEFAULT_AFI = paths.stock_afi()
ATJBOOTTOOL = paths.atjboottool() or Path("/nonexistent")


def load_inputs(a):
    paths.need(a.fw, "official firmware", paths.FETCH_HINT)
    paths.need(a.afi, "firmware database", paths.FETCH_HINT)
    fw = Path(a.fw).read_bytes()
    if a02fw.sha256(fw) != a02fw.STOCK_FW_SHA:
        sys.exit("official .fw hash mismatch: %s" % a.fw)
    if a02fw.sha256(Path(a.afi).read_bytes()) != a02fw.STOCK_AFI_SHA:
        sys.exit("firmware database hash mismatch: %s" % a.afi)
    return fw, a02fw.FirmwareDB(a.afi)


def device_check(hexbytes, hexpath, expect_stock=False, db=None, log=print):
    """Replay what fwupdate.AP + FWDec.al do with the file, fail on anything they would reject."""
    h = hexbytes
    if len(h) < 0x4800:
        raise ValueError("file shorter than the 0x4800-byte first read")
    if len(h) % 512:
        raise ValueError("file size is not a whole number of 512-byte sectors (truncated copy?)")
    if h[:16] != a02fw.FWU_SIG:
        raise ValueError("FWU magic")
    if struct.unpack_from("<I", h, 0x14)[0] != 0x200 or h[0x18] != 0x7E or h[0x19] != 0xE1:
        raise ValueError("FWU header (block size / version / type)")
    if h[0x1A:0x2A] != a02fw.FWU_SIG2:
        raise ValueError("FWU second signature")
    key, _, _ = a02fw.session_key(hexpath)            # ECIES check on OUR file's envelope
    log("  envelope + session key: ok")

    plain = a02fw.unwrap(h, key)
    first = plain[:0x4000]
    ents = {}
    for off in range(0, 0x4000, 0x20):
        n = first[off:off + 11]
        if n in (b"FWIMAGE FW ", b"FLASH_IDBIN") and n not in ents:
            ents[n] = struct.unpack_from("<II", first, off + 0x10)
    if len(ents) != 2:
        raise ValueError("FWIMAGE.FW / FLASH_ID.BIN not found in the first 0x4000 bytes")
    (fo, fl), (io, il) = ents[b"FWIMAGE FW "], ents[b"FLASH_IDBIN"]
    for o, ln in ((fo, fl), (io, il)):
        if o % 512 or o + 0x800 + ln > len(h):
            raise ValueError("entry offset/size out of range")
    log("  directory: FWIMAGE.FW %d bytes, FLASH_ID.BIN %d bytes" % (fl, il))

    if a02fw.NAND_ID not in plain[io:io + il]:
        raise ValueError("this unit's NAND ID is not in FLASH_ID.BIN")
    log("  NAND ID %s listed in FLASH_ID.BIN: ok" % a02fw.NAND_ID.hex())

    lfi = plain[fo:fo + fl]
    d = a02fw.parse_lfi(lfi)
    log("  LFI: header/dir checksums ok, %d files, every file checksum ok" % len(d))
    end = max(sec * 512 + ln for _, sec, ln, _ in d)
    if end > fl:
        raise ValueError("LFI files extend past FWIMAGE.FW")

    # the updater copies its own header fields into 0x28-0x37/0x4f/0x50-0x7f and recomputes 0x1fe;
    # our build must already hold the same values the device has, so that step is a no-op.
    if expect_stock:
        if struct.unpack_from("<I", lfi, 0x10)[0] != a02fw.STOCK_LFI_DIR_SUM or fl != a02fw.STOCK_LFI_SIZE:
            raise ValueError("not byte-identical to the firmware installed on the player")
        ref = a02fw.build_lfi(db)
        if lfi != ref:
            raise ValueError("FWIMAGE.FW differs from the stock rebuild")
        log("  == stock: dir sum 0x%08x, size 0x%x — same image the player has now" % (
            a02fw.STOCK_LFI_DIR_SUM, fl))
    return lfi


def rockbox_check(hexpath, lfi, log=print):
    """Independent decryption with Rockbox's atjboottool (auto-detects the ATJ2127 cipher)."""
    if not ATJBOOTTOOL.exists():
        log("  (atjboottool not built; skipping independent check)")
        return
    with tempfile.TemporaryDirectory() as t:
        afi = Path(t) / "out.afi"
        r = subprocess.run([str(ATJBOOTTOOL), "-c", "--fwu", "-o", str(afi), str(hexpath)],
                           capture_output=True, text=True)
        if r.returncode or "ATJ2127 firmware" not in r.stdout:
            raise ValueError("atjboottool could not decrypt the file:\n" + r.stdout[-2000:])
        r = subprocess.run([str(ATJBOOTTOOL), "-c", "--afi", "-o", t + "/x", str(afi)],
                           capture_output=True, text=True, cwd=t)
        if r.returncode:
            raise ValueError("atjboottool rejected the AFI:\n" + r.stdout[-2000:])
        got = list(Path(t).glob("x*FWIMAGE.FW")) + list(Path(t).rglob("FWIMAGE.FW"))
        if not got or got[0].read_bytes() != lfi:
            raise ValueError("atjboottool's FWIMAGE.FW differs from ours")
    log("  Rockbox atjboottool: decrypts, AFI checksums ok, FWIMAGE.FW identical")


def make(a, replace=None, add=None, expect_stock=False):
    fw, db = load_inputs(a)
    key, _, _ = a02fw.session_key(a.fw)
    lfi = a02fw.build_lfi(db, replace=replace, add=add)
    a02fw.parse_lfi(lfi)
    flash_id = dict(db.files("NAND_ID"))["flash_id.bin"]
    afi = a02fw.build_afi([("FWIMAGE.FW", lfi), ("FLASH_ID.BIN", flash_id)])
    out = a02fw.wrap(afi, fw, key)
    dest = Path(a.output)
    tmp = dest.with_name(dest.name + ".part")
    tmp.write_bytes(out)
    try:
        print("verifying %s (%d bytes)" % (dest, len(out)))
        device_check(out, tmp, expect_stock=expect_stock, db=db)
        rockbox_check(tmp, lfi)
    except Exception as e:
        tmp.unlink()
        sys.exit("REFUSED: %s: %s" % (type(e).__name__, e))
    tmp.replace(dest)
    print("OK  %s  sha256 %s" % (dest, a02fw.sha256(out)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fw", default=str(DEFAULT_FW))
    ap.add_argument("--afi", default=str(DEFAULT_AFI))
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("stock")
    s.add_argument("-o", "--output", default="UPGRADE.HEX")
    b = sub.add_parser("build")
    b.add_argument("-o", "--output", default="UPGRADE.HEX")
    b.add_argument("--replace", action="append", default=[], metavar="NAME=PATH")
    b.add_argument("--add", action="append", default=[], metavar="NAME=PATH")
    v = sub.add_parser("verify")
    v.add_argument("file")
    v.add_argument("--expect-stock", action="store_true")
    a = ap.parse_args()

    if a.cmd == "stock":
        make(a, expect_stock=True)
    elif a.cmd == "build":
        rep = {k: Path(p).read_bytes() for k, p in (x.split("=", 1) for x in a.replace)}
        add = [(k, Path(p).read_bytes()) for k, p in (x.split("=", 1) for x in a.add)]
        make(a, replace=rep, add=add)
    else:
        _, db = load_inputs(a)
        data = Path(a.file).read_bytes()
        try:
            lfi = device_check(data, a.file, expect_stock=a.expect_stock, db=db)
            rockbox_check(a.file, lfi)
        except Exception as e:
            sys.exit("FAIL: %s: %s" % (type(e).__name__, e))
        print("OK")


if __name__ == "__main__":
    main()

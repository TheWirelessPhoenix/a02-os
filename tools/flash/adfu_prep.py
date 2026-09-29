#!/usr/bin/env python3
"""Prepare the files for the vendor USB (ADFU) flash procedure, reimplemented for macOS.

The Windows "Audio Products Volume-Production tools" run a script stored INSIDE the firmware
database (tables FuncSpec/ExSymbol, function `AdfuUpgrade`; dump: re/vendor-script/). This
module reproduces what that script computes, so actions_dump can replay it from a Mac:

  1. ADFUS -> 0x118000, switch                                   (already our normal loader)
  2. nandhwsc.bin -> 0x11E000, call, read 0x9C-byte HWScanInfo    (+16 flash id, +0x18 param addr)
  3. SERACH_ID: 128-byte chip record from FLASH_ID.BIN -> param addr (0x125400 on the ATJ2157)
     <- the step our first attempt skipped (the driver then hung)
  4. FWSC (fwscf651 for this NAND) with header patches -> 0x11E000, call +0x200, read status
  5. storage writes: 0x40.. MBREC, 0x46.. BREC, 0xC0.. LFI (2 MB chunks), 0xFF.. finalize;
     reads: 0x80.. LFI

Every generated piece can be cross-checked against a boot record read from your own unit (--brec-backup),
which the vendor tool wrote with the same values.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import a02fw  # noqa: E402
import paths  # noqa: E402

REPO = paths.REPO
NAF_PARAM_ADDR = 0x125400          # nandhwsc.bin: HWScanInfo+0x18 (literal at 0x11E084)
FWSC_ADDR = 0x11E000
HWSC_ADDR = 0x11E000


def search_id(flash_id_bin, id8):
    """SERACH_ID(): first 128-byte record (from offset 512) whose 8 ID bytes match, 0xFF = any.
    Returns (record, flash_type)."""
    for off in range(512, len(flash_id_bin) - 127, 128):
        rec = flash_id_bin[off:off + 128]
        if all(rec[i] == 0xFF or rec[i] == id8[i] for i in range(8)):
            break
    else:
        raise ValueError("NAND ID %s not in FLASH_ID.BIN" % id8.hex())
    if not rec[22] & 8:
        return rec, 0xF644
    m, d5 = rec[0], rec[5]
    table = [
        (m == 0xAD and d5 & 7 == 3, 0xF645), (m == 0x98, 0xF646), (m == 0xAD and d5 & 7 == 4, 0xF647),
        (m == 0xAD and d5 & 15 == 10, 0xF655), (m == 0xEC and d5 & 7 == 4, 0xF648), (m == 0x2C, 0xF649),
        (m == 0x89, 0xF651), (m == 0x45 and d5 == 0x57, 0xF650), (m == 0x45 and d5 == 0x50, 0xF652),
        (m == 0x45 and d5 == 0x56, 0xF653), (m == 0x45 and d5 & 15 == 1, 0xF654)]
    for cond, t in table:
        if cond:
            return rec, t
    return rec, 0xF644


def mfp_data(db):
    """Read_MFP_config_info(): 64 bytes of pin-config values picked out of the config file."""
    out = bytearray(b"\xff" * 64)
    idx = db.value("INF_PARSE_CONFIG_INDEX")
    cfgname = db.value("CFGF")
    if idx is None or cfgname is None:
        return bytes(out)
    idx = str(idx).zfill(5)
    cnt, start = int(idx[0:2]), int(idx[2:5])
    cfg = dict(db.files("CFGF"))[cfgname]
    for loop in range(cnt):
        off = struct.unpack_from("<I", cfg, 16 + (start + loop) * 8)[0]
        out[loop] = cfg[off]
    return bytes(out)


def file_by_key(db, table, name):
    return dict(db.files(table))[name]


def align(b, n):
    return b + b"\0" * (-len(b) % n)


def brec_image(db, lfi_sectors, flash_type=0xF651):
    """BREC(subtype) as the script builds it (header fields only matter for validation here)."""
    name = [r for r in db.db.execute("select FileName, NumberD from BREC") if r[1] == flash_type][0][0]
    brec = bytearray(align(file_by_key(db, "BREC", name), 1024))
    welcome = align(file_by_key(db, "PLOG", db.value("PLOG")), 512)
    brec[0x1000:0x1000 + len(welcome)] = welcome
    res = align(file_by_key(db, "WELD", db.value("WELD")), 512)
    struct.pack_into("<HH", brec, 12, len(brec) // 512, len(res) // 512)
    struct.pack_into("<H", brec, 4, len(brec) // 512)
    struct.pack_into("<H", brec, 6, (len(brec) + len(res)) // 512)
    brec[24] = db.value("INF_REC_RDN_SUPPORT") or 0
    brec[64:128] = mfp_data(db)
    struct.pack_into("<I", brec, 8, lfi_sectors)
    return brec, res


def fwsc_image(db, driver, brec_cap, lfi_sectors):
    """FWSC(subtype) + AdfuUpgrade's patches. `driver` = fwscf651 bytes (use the read-only copy)."""
    f = bytearray(driver)
    f[128:192] = mfp_data(db)
    if db.value("INF_AUTOREBOOT") is not None:
        f[255] = db.value("INF_AUTOREBOOT")
    if db.value("INF_FWSC_IC_CHECK") is not None:
        f[254] = db.value("INF_FWSC_IC_CHECK")
    if db.value("INF_PROG_SPEEDUP") is not None:
        f[253] = db.value("INF_PROG_SPEEDUP")
    struct.pack_into("<H", f, 6, brec_cap)
    struct.pack_into("<I", f, 8, lfi_sectors)
    f[2] = 0                          # FLASH_ERASE flag: never erase
    return bytes(f)


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--afi", default=str(paths.stock_afi()))
    ap.add_argument("--brec-backup", default=str(paths.brec_backup() or ""),
                    help="this unit's boot record read from NAND; enables the extra cross-checks")
    ap.add_argument("--driver", default=str(paths.readonly_driver()))
    ap.add_argument("-o", "--outdir", required=True)
    a = ap.parse_args()
    db = a02fw.FirmwareDB(paths.need(a.afi, "firmware database", paths.FETCH_HINT))
    out = Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)

    lfi = a02fw.build_lfi(db)
    lfi_sectors = len(lfi) // 512
    rec, ftype = search_id(dict(db.files("NAND_ID"))["flash_id.bin"], bytes.fromhex("8984643ca50c0000"))
    brec, res = brec_image(db, lfi_sectors, ftype)
    brec_cap = (len(brec) + len(res)) // 512

    ok = True
    checks = [("flash type", ftype == 0xF651)]
    if a.brec_backup and Path(a.brec_backup).exists():
        real = Path(a.brec_backup).read_bytes()
    else:
        real = None
        print("  (no boot-record backup given: skipping the cross-checks against this unit's BREC)")
    checks += [] if real is None else [
              ("sizes +4/+6/+8/+12/+14", real[4:16] == bytes(brec[4:16])),
              ("pin config +0x40", real[0x40:0x80] == bytes(brec[0x40:0x80])),
              # BREC +0xC0/+0xE0 (capacity info, key data) are written over the record's 2nd half
              ("chip record +0x80 == SERACH_ID result", real[0x80:0xC0] == rec[:64]),
              ("welcome logo at +0x1000", real[0x1000:0x1000 + 3584] == bytes(brec[0x1000:0x1000 + 3584])),
              ("boot code 0x200..0x1000", real[0x200:0x1000] == bytes(brec[0x200:0x1000]))]
    for name, good in checks:
        print("  %-40s %s" % (name, "ok" if good else "MISMATCH"))
        ok &= good
    if not ok:
        sys.exit("REFUSED: generated values differ from this unit's boot record")

    drv = paths.need(a.driver, "flash driver", paths.FETCH_HINT).read_bytes()
    (out / "nandinfo.bin").write_bytes(rec)
    (out / "fwsc-ro.bin").write_bytes(fwsc_image(db, drv, brec_cap, lfi_sectors))
    (out / "lfi-expected.bin").write_bytes(lfi)
    print("wrote %s: nandinfo.bin (128), fwsc-ro.bin (%d), lfi-expected.bin (%d); brec_cap %d, lfi %d sectors"
          % (out, len(drv), len(lfi), brec_cap, lfi_sectors))


if __name__ == "__main__":
    main()

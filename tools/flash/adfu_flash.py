#!/usr/bin/env python3
"""Write the firmware image (LFI) over USB recovery (ADFU) from a Mac — the vendor procedure.

Same sequence as adfu_dryrun.py (proven on the device 2026-09-28), but with the vendor's own
unmodified driver, then:
  - read the device's current LFI header and keep its per-device fields (the vendor script does
    this: +0x28 capacity info, +0x4f/+0x50 USB serial) and fix the header checksum;
  - write the LFI only (storage type 0xC0) — the boot records (MBREC/BREC) are NOT touched;
  - finalize (type 0xFF);
  - read the whole LFI back and compare BEFORE any reboot. On a mismatch: leave the player
    plugged in and in recovery, and run this again.

  adfu_flash.py --write [--image lfi.bin]      (default image: the official firmware, rebuilt)

Needs: player in USB recovery (10d6:10d6) from a cold boot. Afterwards: switch OFF, unplug,
replug, ON.
"""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import paths  # noqa: E402
REPO = paths.REPO
FILES = paths.vendor_files()
import a02fw  # noqa: E402
import adfu_prep  # noqa: E402
from adfu_dryrun import run, hexdump_bytes  # noqa: E402

STOCK_FWSC_SHA = "93c4686260826e635babb16226e1a348a971892ef09fcdaaa4f70bfde4d7bcf9"  # stock fwscf651.bin
PRESERVE = [(0x28, 0x38), (0x4F, 0x80)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true", help="required: actually write the flash")
    ap.add_argument("--image", help="LFI to write (default: official firmware rebuilt)")
    ap.add_argument("--work", default=str(paths.vendor_dir() / "work" / "adfu-flash"))
    a = ap.parse_args()
    if not a.write:
        sys.exit("refusing without --write (use adfu_dryrun.py for a read-only rehearsal)")

    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    db = a02fw.FirmwareDB(paths.need(paths.stock_afi(), "firmware database", paths.FETCH_HINT))
    image = bytearray(Path(a.image).read_bytes() if a.image else a02fw.build_lfi(db))
    a02fw.parse_lfi(bytes(image))                         # checksums of every file
    if len(image) % 512:
        sys.exit("image is not a whole number of sectors")

    # stock driver, with the same header patches the vendor script applies
    stock = FILES / "fwscf651.bin"
    if a02fw.sha256(stock.read_bytes()) != STOCK_FWSC_SHA:
        sys.exit("fwscf651.bin is not the stock driver")
    subprocess.run([sys.executable, str(HERE / "adfu_prep.py"), "-o", str(work), "--driver", str(stock)],
                   check=True)
    (work / "fwsc.bin").write_bytes((work / "fwsc-ro.bin").read_bytes())
    (work / "fwsc-ro.bin").unlink()
    vram = bytearray(512)
    vram[510:512] = b"\xaa\xbb"                           # VRAM_LAST_SECTOR(0)
    (work / "vram_last.bin").write_bytes(vram)

    run(["simple_switch", "0x118000", "payload_arm/adfus.bin"], "1/7 loader (adfus)")
    hw = hexdump_bytes(run(["write_mem", "0x11e000", "0", "0", str(FILES / "nandhwsc.bin"),
                            "exec_ret", "0x11e000", "-1"], "2/7 hardware scan"))
    if hw[0:2] != b"HW" or hw[16:21] != a02fw.NAND_ID or \
            int.from_bytes(hw[0x18:0x1C], "little") != adfu_prep.NAF_PARAM_ADDR:
        sys.exit("STOP: unexpected hardware scan %s" % hw[:32].hex())
    st = hexdump_bytes(run(["write_mem", "0x11e000", "0", "0", str(work / "fwsc.bin"),
                            "write_mem", hex(adfu_prep.NAF_PARAM_ADDR), "0", "0", str(work / "nandinfo.bin"),
                            "exec_ret", "0x11e200", "-1"], "3/7 flash driver + chip record"))
    if st[6:12] != b"NAFWSC":
        sys.exit("STOP: driver status %s" % st[:32].hex())

    run(["read_lfi", "0", "0x200", str(work / "device_head.bin")], "4/7 read the current header")
    dev = (work / "device_head.bin").read_bytes()
    if struct.unpack_from("<I", dev, 0)[0] != a02fw.LFI_MAGIC:
        sys.exit("STOP: device header has no LFI magic (%s)" % dev[:16].hex())
    for s, e in PRESERVE:
        image[s:e] = dev[s:e]
    struct.pack_into("<H", image, 0x1FE, a02fw.sum16(bytes(image[:0x1FE])))
    a02fw.parse_lfi(bytes(image))
    (work / "image.bin").write_bytes(image)
    print("   image: %d sectors, sha256 %s" % (len(image) // 512, a02fw.sha256(bytes(image))))

    run(["blk_size", "0x4000", "write_flash", "0xc0000000", "0", "0", str(work / "image.bin")],
        "5/7 WRITE firmware area (LFI only)")
    run(["write_flash", "0xff000000", "0", "0", str(work / "vram_last.bin")], "6/7 finalize")

    run(["blk_size", "0x4000", "read_lfi", "0", hex(len(image)), str(work / "readback.bin")],
        "7/7 read back and compare")
    back = (work / "readback.bin").read_bytes()
    bad = [i // 512 for i in range(0, len(image), 512) if back[i:i + 512] != image[i:i + 512]]
    if bad:
        print("\nVERIFY FAILED: %d sectors differ (first %s)." % (len(bad), bad[:8]))
        sys.exit("Leave the player plugged in and in recovery. Do NOT power it off. Run this again.")
    print("\nVERIFIED: all %d sectors read back identical." % (len(image) // 512))
    print("Now: switch OFF, unplug, replug, ON.")


if __name__ == "__main__":
    main()

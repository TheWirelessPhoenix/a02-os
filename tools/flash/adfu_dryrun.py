#!/usr/bin/env python3
"""READ-ONLY rehearsal of the vendor USB flash procedure (tools/flash/adfu_prep.py), from a Mac.

Replays AdfuUpgrade up to the point where it would write, using the gate-proven read-only
driver (erase/program are `return` stubs, see tools/unbrick/nand_gate.py), then reads the whole
firmware image (LFI) back through the driver's own read path and compares it with our rebuild.
Nothing in this session can change the NAND.

Needs: player in USB recovery (ADFU, 10d6:10d6) from a COLD boot (power switch cycle first).
Stops at the first unexpected answer. Afterwards: power switch OFF, unplug, replug, ON.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import paths  # noqa: E402
REPO = paths.REPO
AD = paths.actions_dump_dir()
FILES = paths.vendor_files()
import adfu_prep  # noqa: E402
import a02fw  # noqa: E402


def run(args, what):
    paths.need(AD / "actions_dump", "actions_dump", "Set ACTIONS_DUMP_DIR to a built actions_dump checkout")
    cmd = ["./actions_dump", "--wait", "20", "chip", "2157", "timeout", "60000"] + args
    print("\n== %s\n   %s" % (what, " ".join(cmd)))
    r = subprocess.run(cmd, cwd=AD, capture_output=True, text=True, timeout=900)
    out = r.stdout + r.stderr
    print("   " + "\n   ".join(out.strip().splitlines()[-12:]))
    if r.returncode or "failed" in out or "unexpected" in out:
        sys.exit("STOP: %s did not complete. Power-cycle the player (switch OFF, unplug, replug, ON)." % what)
    return out


def hexdump_bytes(out):
    data = bytearray()
    for line in out.splitlines():
        m = re.match(r"^((?:[0-9a-f]{2} ){1,16})\s*\|", line)
        if m:
            data += bytes.fromhex(m.group(1).replace(" ", ""))
    return bytes(data)


def main():
    work = Path(sys.argv[1] if len(sys.argv) > 1 else paths.vendor_dir() / "work" / "adfu-dryrun")
    work.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(HERE / "adfu_prep.py"), "-o", str(work)], check=True)
    gate = subprocess.run([sys.executable, str(REPO / "tools/unbrick/nand_gate.py"), str(work / "fwsc-ro.bin")],
                          capture_output=True, text=True)
    if "VERDICT: PASS" not in gate.stdout:
        sys.exit("STOP: driver failed the read-only gate")
    print("read-only gate: PASS (driver cannot erase or program)")

    run(["simple_switch", "0x118000", "payload_arm/adfus.bin"], "1/5 loader (adfus)")

    out = run(["write_mem", "0x11e000", "0", "0", str(FILES / "nandhwsc.bin"), "exec_ret", "0x11e000", "-1"],
              "2/5 hardware scan (nandhwsc)")
    hw = hexdump_bytes(out)
    if len(hw) < 0x20 or hw[0:2] != b"HW" or hw[16:21] != a02fw.NAND_ID:
        sys.exit("STOP: unexpected HWScanInfo %s" % hw[:32].hex())
    param = int.from_bytes(hw[0x18:0x1C], "little")
    if param != adfu_prep.NAF_PARAM_ADDR:
        sys.exit("STOP: param address 0x%x, expected 0x%x" % (param, adfu_prep.NAF_PARAM_ADDR))
    print("   HW scan ok: NAND %s, chip-record slot 0x%x" % (hw[16:22].hex(), param))

    out = run(["write_mem", "0x11e000", "0", "0", str(work / "fwsc-ro.bin"),
               "write_mem", hex(param), "0", "0", str(work / "nandinfo.bin"),
               "exec_ret", "0x11e200", "-1"], "3/5 read-only flash driver + chip record")
    st = hexdump_bytes(out)
    print("   driver status (%d bytes): %s" % (len(st), st[:32].hex()))
    (work / "fwsc_status.bin").write_bytes(st)

    run(["read_lfi", "0", "0x2000", str(work / "lfi_head.bin")], "4/5 read firmware header")
    head = (work / "lfi_head.bin").read_bytes()
    exp = (work / "lfi-expected.bin").read_bytes()
    if head[:0x200] != exp[:0x200]:
        sys.exit("STOP: header differs from the expected image (saved lfi_head.bin)")
    print("   header + directory: identical to our rebuild" if head == exp[:0x2000] else
          "   header ok; directory differs (saved)")

    run(["blk_size", "0x4000", "read_lfi", "0", hex(len(exp)), str(work / "lfi_full.bin")],
        "5/5 read the whole firmware image (~59 MB)")
    full = (work / "lfi_full.bin").read_bytes()
    diff = sum(1 for i in range(0, len(exp), 512) if full[i:i + 512] != exp[i:i + 512])
    print("\nRESULT: %d of %d sectors differ from our rebuild" % (diff, len(exp) // 512))
    print("full logical backup: %s  sha256 %s" % (work / "lfi_full.bin", a02fw.sha256(full)))
    print("Now: power switch OFF, unplug, replug, ON (the read session leaves recovery half-working).")


if __name__ == "__main__":
    main()

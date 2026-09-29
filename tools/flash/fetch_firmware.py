#!/usr/bin/env python3
"""Fetch the official AGPTEK A02 firmware (1.101.37, 2024-10-19) into the local vendor cache.

Nothing from the vendor is stored in this repository. This downloads the official update from
AGPTEK's own link (Wayback Machine as fallback), checks SHA-256 before unpacking anything,
decrypts it with Rockbox's atjboottool and writes, outside the repository:
  A02_20241019.fw   the official update (input of mkupgrade.py / adfu_prep.py)
  a02.afi           its decrypted file database (SQLite)
  files/            every file inside it (nandhwsc.bin, fwscf651.bin, KER_INIT.BIN, ...)
  fwscf651-readonly.bin  the flash driver with erase/program disabled (tools/unbrick), gate-checked

  fetch_firmware.py [--rar local.rar] [--atjboottool PATH]
"""
import argparse
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "unbrick"))
import paths  # noqa: E402
import vendor_driver as vd  # noqa: E402  (download + hash pins + read-only driver patch)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rar", help="local copy of the official .rar (still hash-checked)")
    ap.add_argument("--atjboottool", default=str(paths.atjboottool() or ""))
    a = ap.parse_args()
    atj = paths.need(a.atjboottool, "Rockbox atjboottool",
                     "Build it: git clone https://github.com/Rockbox/rockbox && "
                     "make -C rockbox/utils/atj2137/atjboottool, then pass --atjboottool")
    out = paths.vendor_dir()
    out.mkdir(parents=True, exist_ok=True)

    rar = out / vd.RAR_NAME
    if a.rar:
        data = Path(a.rar).read_bytes()
        if vd.sha256(data) != vd.RAR_SHA:
            sys.exit("the given .rar is not the official 20241019 update (SHA-256 mismatch)")
        rar.write_bytes(data)
    elif not rar.exists() or vd.sha256(rar.read_bytes()) != vd.RAR_SHA:
        vd.download(rar)

    fw = subprocess.run(["bsdtar", "-xOf", str(rar), vd.FW_MEMBER], check=True, capture_output=True).stdout
    if vd.sha256(fw) != vd.FW_SHA:
        sys.exit("firmware inside the .rar has an unexpected hash")
    (out / "A02_20241019.fw").write_bytes(fw)
    with tempfile.TemporaryDirectory() as t:
        afi = Path(t) / "a02.afi"
        subprocess.run([str(atj), "--fwu", "-o", str(afi), str(out / "A02_20241019.fw")],
                       check=True, capture_output=True)
        data = afi.read_bytes()
    if vd.sha256(data) != vd.AFI_SHA:
        sys.exit("decrypted database has an unexpected hash")
    (out / "a02.afi").write_bytes(data)

    files = out / "files"
    files.mkdir(exist_ok=True)
    con = sqlite3.connect("file:%s?mode=ro" % (out / "a02.afi"), uri=True)
    n = 0
    for name, blob in con.execute("SELECT FileName, File FROM FileTable"):
        if "/" in name or name.startswith("."):
            sys.exit("unexpected file name in the database: %r" % name)
        (files / name).write_bytes(blob)
        n += 1
    con.close()
    subprocess.run([sys.executable, str(HERE.parent / "unbrick" / "vendor_driver.py"),
                    "--rar", str(rar), "--atjboottool", str(atj)], check=True, capture_output=True)
    print("OK: %s (%d files, read-only driver %s)" % (out, n, paths.readonly_driver().exists()))


if __name__ == "__main__":
    main()

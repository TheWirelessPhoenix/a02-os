#!/usr/bin/env python3
"""Fetch the official AGPTEK A02 firmware, extract the stock flash driver and make a READ-ONLY copy.

Nothing vendor-owned is stored in this repository. This tool downloads the official update from
AGPTEK's own site (falling back to the Internet Archive's copy of the same URLs), verifies SHA-256 at
every step, extracts fwscf651.bin on this machine into a cache OUTSIDE the repository, and applies
patches/fwscf651-readonly.json (the driver's erase/program functions become no-ops).

Sources are a fixed allowlist. The vendor moved the file once already (2026-09-27: the old
images.agptek.us host stopped serving it), so the list carries both locations; the SHA-256 below is
the trust anchor, which makes any mirror of the same bytes acceptable.

Network rules (see the downloader audit notes in docs/FLASH-INSTALL.md): HTTPS only, fixed URL
allowlist, the download is hashed before anything is unpacked, and nothing downloaded is executed on
this computer (the patched driver only ever runs on the player, from RAM).

Needs: python3, bsdtar (macOS built-in), and Rockbox's atjboottool (pass --atjboottool or set
ATJBOOTTOOL; build: https://github.com/Rockbox/rockbox utils/atj2137/atjboottool, `make`).
Usage: vendor_driver.py [--rar local.rar] [--atjboottool PATH]   -> prints the patched driver path
"""
import argparse
import hashlib
import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import nand_gate  # noqa: E402  (same directory)
RAR_NAME = "AGPTEK_A02(20241019).rar"
# Current vendor location, taken from AGPTEK's own firmware page
# (https://www.agptek.com/agptek_a02-firmware-upgrade-2/, "AGP-A02-20241019-V1.0").
VENDOR = "https://www.hommiehk.com/mbt-image/Download/" + RAR_NAME
# Legacy host: AGPTEK's old CDN. It still resolves but its TLS certificate no longer matches the
# hostname, so this entry normally fails and the next source is tried. Kept for the record.
LEGACY = "https://www.images.agptek.us/Download/" + RAR_NAME
URLS = [
    VENDOR,
    "https://web.archive.org/web/2025id_/" + VENDOR,   # id_ = original bytes, no rewriting
    "https://web.archive.org/web/2025id_/" + LEGACY,
    LEGACY,
]
ALLOWED_HOSTS = {"www.hommiehk.com", "web.archive.org", "www.images.agptek.us"}
RAR_SHA = "56b1feb77c1b81411c767bb57c86f3f82464974dbc8a200426fcf174dadac6d8"
FW_MEMBER = "AGPTEK-A02(20241019)"
FW_SHA = "45b198455d310c15030506c1cefb654e65e505298d0cfa167fdfe1d550586484"
AFI_SHA = "dd29194287946d6bf8c8e7ea127d990fd8616b0ab1f5c0925898b50facd9cbf1"
DRIVER = "fwscf651.bin"
MAX_DOWNLOAD = 120 * 1024 * 1024


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def cache_dir():
    base = os.environ.get("XDG_CACHE_HOME") or (
        Path.home() / "Library" / "Caches" if sys.platform == "darwin" else Path.home() / ".cache")
    d = Path(os.environ.get("A02_VENDOR_DIR") or Path(base) / "a02-os" / "vendor")   # = tools/flash/paths.py
    d.mkdir(parents=True, exist_ok=True)
    if REPO in d.resolve().parents:
        sys.exit("refusing to cache vendor files inside the repository")
    return d


class NoRedirectOffAllowlist(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        check_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def check_url(url):
    u = urllib.parse.urlparse(url)
    if u.scheme != "https" or u.hostname not in ALLOWED_HOSTS:
        raise ValueError(f"blocked URL (not HTTPS or not allowlisted): {url}")


def download(dest):
    opener = urllib.request.build_opener(NoRedirectOffAllowlist)
    for url in URLS:
        check_url(url)
        print(f"downloading {url}", file=sys.stderr)
        try:
            with opener.open(url, timeout=60) as r:
                data = r.read(MAX_DOWNLOAD + 1)
        except Exception as e:  # try the next source
            print(f"  failed: {e}", file=sys.stderr)
            continue
        if len(data) > MAX_DOWNLOAD:
            print("  too large, ignored", file=sys.stderr)
            continue
        if sha256(data) != RAR_SHA:
            print("  SHA-256 mismatch, ignored", file=sys.stderr)
            continue
        dest.write_bytes(data)
        return
    sys.exit("could not obtain a verified copy of the official firmware")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rar", help="use a local copy of the official .rar instead of downloading")
    ap.add_argument("--atjboottool", default=os.environ.get("ATJBOOTTOOL"))
    a = ap.parse_args()
    spec = json.loads((HERE / "patches" / "fwscf651-readonly.json").read_text())
    cache = cache_dir()
    out = cache / "fwscf651-readonly.bin"
    if out.exists() and sha256(out.read_bytes()) == spec["output_sha256"]:
        print(out)
        return

    rar = cache / RAR_NAME
    if a.rar:
        data = Path(a.rar).read_bytes()
        if sha256(data) != RAR_SHA:
            sys.exit("the given .rar is not the official 20241019 update (SHA-256 mismatch)")
        rar.write_bytes(data)
    elif not rar.exists() or sha256(rar.read_bytes()) != RAR_SHA:
        download(rar)

    fw = subprocess.run(["bsdtar", "-xOf", str(rar), FW_MEMBER], check=True, capture_output=True).stdout
    if sha256(fw) != FW_SHA:
        sys.exit("firmware inside the .rar has an unexpected hash")
    if not a.atjboottool:
        sys.exit("need Rockbox atjboottool: pass --atjboottool PATH or set ATJBOOTTOOL")
    with tempfile.TemporaryDirectory() as t:
        fwp, afi = Path(t) / "a02.fw", Path(t) / "a02.afi"
        fwp.write_bytes(fw)
        subprocess.run([a.atjboottool, "--fwu", "-o", str(afi), str(fwp)], check=True, capture_output=True)
        if sha256(afi.read_bytes()) != AFI_SHA:
            sys.exit("decrypted AFI has an unexpected hash")
        con = sqlite3.connect(f"file:{afi}?mode=ro", uri=True)
        row = con.execute("SELECT File FROM FileTable WHERE FileName = ?", (DRIVER,)).fetchone()
        con.close()
    if not row:
        sys.exit(f"{DRIVER} not found in the update")
    drv = bytearray(row[0])
    if len(drv) != spec["input_size"] or sha256(drv) != spec["input_sha256"]:
        sys.exit(f"{DRIVER} has an unexpected hash")

    load_addr = int(spec["load_address"], 16)

    # Self-test (fail closed): the gate must REJECT the unpatched driver, proving
    # it actually detects erase/program. If it accepts the stock driver the gate is
    # broken and we must not trust its verdict on the patched one either.
    with tempfile.TemporaryDirectory() as t:
        stock = Path(t) / "unpatched.bin"
        stock.write_bytes(drv)
        try:
            nand_gate.scan_nand_gate(str(stock), load_addr, quiet=True)
        except nand_gate.GateFailedError:
            pass
        else:
            sys.exit("gate self-test failed: the unpatched driver passed the read-only "
                     "gate, so the gate cannot be trusted; refusing")

    for p in spec["patches"]:
        o, exp, rep = int(p["offset"], 16), bytes.fromhex(p["expect"]), bytes.fromhex(p["replace"])
        if drv[o:o + len(exp)] != exp:
            sys.exit(f"patch '{p['name']}': original bytes differ, refusing")
        drv[o:o + len(rep)] = rep
    if sha256(drv) != spec["output_sha256"]:
        sys.exit("patched driver hash mismatch")

    # The device-run gate: the patched driver must be proven incapable of writing
    # NAND before it is cached (and later loaded onto the player).
    with tempfile.TemporaryDirectory() as t:
        patched = Path(t) / "patched.bin"
        patched.write_bytes(drv)
        try:
            nand_gate.scan_nand_gate(str(patched), load_addr)
        except nand_gate.GateFailedError as e:
            sys.exit(f"refusing to write the patched driver: {e}")

    out.write_bytes(drv)
    print(out)


if __name__ == "__main__":
    main()

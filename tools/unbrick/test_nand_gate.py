#!/usr/bin/env python3
"""Regression test for the NAND read-only gate.

Checks, using the patched driver already in the cache (run vendor_driver.py first):
  * the gate PASSES the patched driver;
  * the gate FAILS the same driver with the two no-op patches reversed (i.e. it
    really detects erase/program capability, not just "looks patched").

Usage: python3 tools/unbrick/test_nand_gate.py [--driver PATH]
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import nand_gate  # noqa: E402


def cache_driver():
    import os
    base = os.environ.get("XDG_CACHE_HOME") or (
        Path.home() / "Library" / "Caches" if sys.platform == "darwin"
        else Path.home() / ".cache")
    return Path(base) / "a02-os" / "vendor" / "fwscf651-readonly.bin"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--driver", help="patched driver (defaults to the cache)")
    a = ap.parse_args()
    spec = json.loads((HERE / "patches" / "fwscf651-readonly.json").read_text())
    load = int(spec["load_address"], 16)

    drv = Path(a.driver) if a.driver else cache_driver()
    if not drv.exists():
        print(f"SKIP: {drv} not found; run vendor_driver.py first", file=sys.stderr)
        return 2

    failures = []

    # 1. patched driver must pass
    try:
        nand_gate.scan_nand_gate(str(drv), load, quiet=True)
        print("PASS  patched driver accepted")
    except nand_gate.GateFailedError as e:
        failures.append(f"patched driver was rejected: {e}")

    # 2. reverse the patches -> must fail
    import tempfile
    rev = bytearray(drv.read_bytes())
    for p in spec["patches"]:
        o = int(p["offset"], 16)
        exp, rep = bytes.fromhex(p["expect"]), bytes.fromhex(p["replace"])
        if rev[o:o + len(rep)] != rep:
            failures.append(f"cache driver does not carry patch '{p['name']}'")
        rev[o:o + len(exp)] = exp
    with tempfile.TemporaryDirectory() as t:
        up = Path(t) / "unpatched.bin"
        up.write_bytes(rev)
        try:
            nand_gate.scan_nand_gate(str(up), load, quiet=True)
            failures.append("unpatched driver was ACCEPTED (gate is not fail-closed)")
        except nand_gate.GateFailedError:
            print("PASS  unpatched driver rejected")

    if failures:
        for f in failures:
            print(f"FAIL  {f}", file=sys.stderr)
        return 1
    print("all gate tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

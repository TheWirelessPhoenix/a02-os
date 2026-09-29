"""Where the flash tools find things. No vendor files live in the public repository: they are
fetched from AGPTEK's official download into a local cache by fetch_firmware.py (hash-checked).
A private checkout with fw/ is used first when present.

Environment overrides: A02_VENDOR_DIR (cache), ACTIONS_DUMP_DIR (actions_dump checkout with a
built ./actions_dump and payload_arm/adfus.bin), ATJBOOTTOOL (Rockbox atjboottool binary),
ATJBOOTTOOL_SRC (its source dir, for building fwkey).
"""
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def vendor_dir():
    base = os.environ.get("A02_VENDOR_DIR")
    if base:
        d = Path(base)
    else:
        root = os.environ.get("XDG_CACHE_HOME") or (
            Path.home() / "Library" / "Caches" if sys.platform == "darwin" else Path.home() / ".cache")
        d = Path(root) / "a02-os" / "vendor"
    if REPO in d.resolve().parents:
        sys.exit("refusing to use a vendor cache inside the repository")
    return d


def _first(*cands):
    for c in cands:
        if c and Path(c).exists():
            return Path(c)
    return Path(cands[-1]) if cands[-1] else None


def stock_fw():
    return _first(REPO / "fw/stock/A02_20241019.fw", vendor_dir() / "A02_20241019.fw")


def stock_afi():
    return _first(REPO / "fw/unpacked/a02.afi", vendor_dir() / "a02.afi")


def vendor_files():
    """Directory holding every file of the official firmware (nandhwsc.bin, fwscf651.bin, ...)."""
    return _first(REPO / "fw/unpacked/files", vendor_dir() / "files")


def readonly_driver():
    return vendor_dir() / "fwscf651-readonly.bin"


def actions_dump_dir():
    return _first(os.environ.get("ACTIONS_DUMP_DIR"), REPO / "tools/actions_flash",
                  REPO.parent / "actions_dump")


def atjboottool():
    return _first(os.environ.get("ATJBOOTTOOL"),
                  REPO / "tools/rockbox/utils/atj2137/atjboottool/atjboottool",
                  REPO.parent / "rockbox/utils/atj2137/atjboottool/atjboottool")


def brec_backup():
    """This unit's boot record, read from its NAND (private checkouts only). Optional."""
    p = REPO / "backup/nand/20260927-194708/brec0.bin"
    return p if p.exists() else None


def need(path, what, hint):
    if not path or not Path(path).exists():
        sys.exit("missing %s (%s). %s" % (what, path, hint))
    return Path(path)


FETCH_HINT = "Run: python3 tools/flash/fetch_firmware.py (downloads the official update, hash-checked)"

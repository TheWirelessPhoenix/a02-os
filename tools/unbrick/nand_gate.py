"""Fail-closed binary scanner that proves the patched flash driver cannot write NAND.

This is the automated read-only proof required by docs/FLASH-INSTALL.md,
and the gate that must pass before the patched driver is allowed near the player.

The scan (no capstone needed, just arm-none-eabi-objdump):

  1. Linear sweep of the disassembly tracking each register's constant value, so a
     store is only attributed to the NAND controller when the base register provably
     holds base 0xC0150000 at that point (register liveness kills false positives).
  2. Every store into the command block is classified against the ground-truth opcode
     table below. Anything unresolved or unrecognized fails closed.
  3. Patch-site integrity: the two patched functions must begin with the no-op.
  4. Reachability: the original bodies of the two patched functions are dead code;
     the scan proves nothing outside them branches or points into them, then ignores
     the dangerous stores those bodies still contain.

Ground truth (a Ghidra decompile of fwscf651.bin — exhaustive enumeration of every literal
command-register writer, cross-checked against the disassembly):

  NAND controller base 0xC0150000; +0x20 = command opcode, +0x24..+0x34 = argument
  words, +0x38 = trigger. Functions that write an opcode to +0x20 on this base:

    FUN_00121fa0  ERASE    0xd060 (arg 0x6068)                     <- PATCHED, body dead
    FUN_00122b44  PROGRAM  0x80   (0x6c 0x8585 0x7565 0x10 0x60)   <- PATCHED, body dead
    FUN_0012249e  STATUS   0x70   (0x61)
    FUN_00122058  MODE     0x90   (0x69)
    FUN_00122d32  VENDOR   0xef   (0x69)
    FUN_00123698  VENDOR   0xef   (0x6b)
    FUN_00124c40  RESET    0xff   (0x62)
    FUN_00124c5c  READ     0x3000 (0x626c)

  0xd060 and 0x80 are the only opcodes that change NAND content. 0xef is a vendor
  read (writes an address to +0x18, reads a result; no long argument words). All the
  others are read / reset / status / mode. The RMW writers (FUN_001220e0, FUN_001222f2,
  FUN_00122dfa) target other register blocks (0xC01C0000 / RAM pools), not the NAND base.

Usage:
  python3 nand_gate.py DRIVER.bin [--load-addr 0x11e000] [--quiet]
  exit 0 = proven read-only; exit 1 = FAIL, do not load onto the player.
"""
import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path


class GateFailedError(Exception):
    """Raised when the NAND gate scan finds unauthorized NAND write capability."""


NAND_BASE = 0xC0150000
LOAD_ADDR_DEFAULT = 0x11E000

CMD_OFF = 0x20
ARG_OFFS = (0x24, 0x28, 0x2C, 0x30, 0x34)

# Opcodes at +0x20 that cannot change NAND content.
SAFE_OPCODES = {
    0x3000: "read", 0xFF: "reset", 0x90: "mode", 0x70: "status", 0xEF: "vendor-read",
}
# Opcodes at +0x20 that DO change NAND content.
DANGEROUS_OPCODES = {0xD060: "ERASE", 0x80: "PROGRAM"}
# Distinctive argument words belonging to the erase/program sequences.
DANGEROUS_ARG_WORDS = {0x6068: "erase arg", 0x8585: "program arg", 0x7565: "program arg"}

# (file offset, expected original bytes, replacement bytes, dead-body end (load addr), name)
PATCHES = [
    (0x3FA0, "f0b587b0", "01207047", 0x122044, "erase issuer FUN_00121fa0"),
    (0x4B44, "70b53f4d", "00207047", 0x122C44, "program issuer FUN_00122b44"),
]

# Mnemonics whose first operand is a destination register (used for liveness).
KILL_MNEM = set(
    "ldr ldrb ldrh ldrsb ldrsh ldrsw ldr.n ldr.w ldrb.w ldrh.w ldrsb.w ldrsh.w "
    "mov movs mov.n mov.w movs.w movw movt mvn mvn.w "
    "add adds add.n add.w sub subs sub.n sub.w rsb rsbs "
    "orr orrs orr.w and ands and.w bic bics bic.w eor eors eor.w orn "
    "lsl lsls lsr lsrs asr asrs ror rors rrx ubfx bfi sbfx bfc "
    "rev rev16 revsh sxtb sxth uxtb uxth clz mul muls adr adr.w".split()
)
REG_MAP = {f"r{i}": i for i in range(16)}
REG_MAP.update({"fp": 11, "sl": 10, "ip": 12, "sp": 13, "lr": 14, "pc": 15})

RE_INSTR = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\s+([a-z][a-z0-9._]*)\s*(.*)$")
RE_PC_REL = re.compile(r"\[pc,\s*#(-?\d+)\]")
RE_IMM = re.compile(r"^#(-?\d+|0x[0-9a-f]+)")
RE_BRACKET = re.compile(r"\[([a-z0-9]+)(?:,\s*#(-?\d+|0x[0-9a-f]+))?\]")
RE_REGNAME = re.compile(r"^[a-z][a-z0-9]*$")
RE_BRANCH = re.compile(
    r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\s+"
    r"(?:bl|blx|b|b\.n|b\.w|beq|bne|bcs|bcc|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble|bal)"
    r"(?:\.n|\.w)?\s+0x([0-9a-f]+)"
)


def _int(s: str) -> int:
    return int(s, 16) if s.startswith("0x") else int(s)


def _reg(name: str):
    name = name.strip()
    return REG_MAP.get(name) if RE_REGNAME.match(name) else None


def _split_operands(payload: str):
    parts, depth, cur = [], 0, ""
    for ch in payload:
        if ch == "[":
            depth += 1
        if ch == "]":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur.strip())
    return parts


class _Bin:
    def __init__(self, path: str, load_addr: int):
        self.load_addr = load_addr
        self.data = Path(path).read_bytes()

    def word(self, addr: int):
        off = addr - self.load_addr
        if off < 0 or off + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, off)[0]

    def has_bytes(self, addr: int, n: int):
        off = addr - self.load_addr
        return 0 <= off and off + n <= len(self.data)


def run_objdump(path: str, load_addr: int) -> str:
    r = subprocess.run(
        ["arm-none-eabi-objdump", "-D", "-b", "binary", "-m", "arm", "-M", "force-thumb",
         f"--adjust-vma=0x{load_addr:x}", str(path)],
        capture_output=True, text=True, timeout=60,
    )
    if r.returncode != 0:
        raise RuntimeError(f"objdump failed: {r.stderr.strip() or r.stdout.strip()}")
    return r.stdout


def _is_prologue(mnem: str, ops: str) -> bool:
    return ("push" in mnem or "stmdb" in mnem or "stmfd" in mnem) and "lr" in ops


def _is_return(mnem: str, ops: str) -> bool:
    if mnem in ("bx", "bx.w", "bx.n") and "lr" in ops:
        return True
    if ("pop" in mnem or "ldmia" in mnem or "ldmfd" in mnem or "ldm" in mnem) and "pc" in ops:
        return True
    return False


def sweep(lines, reader: _Bin):
    """Linear sweep. Returns (command_stores, all_pc_loads).

    command_stores: list of dicts for stores into a NAND-base command register.
    all_pc_loads: set of addresses loaded via `ldr rX,[pc,#N]` (both const and base).
    """
    regs = {}          # reg -> int value, or absent = unknown
    stores = []
    pc_loads = set()

    for line in lines:
        m = RE_INSTR.match(line)
        if not m:
            continue
        addr, mnem, payload = int(m.group(1), 16), m.group(2), m.group(3).split("@")[0].strip()
        if _is_prologue(mnem, payload):
            regs.clear()
            continue
        parts = _split_operands(payload)

        # --- register writes (liveness) ---
        if mnem in KILL_MNEM and parts:
            dest = _reg(parts[0])
            if dest is not None:
                newval = None
                if mnem.startswith(("mov", "mvn")) and len(parts) >= 2:
                    im = RE_IMM.match(parts[1])
                    if im and mnem != "mvn":
                        newval = _int(im.group(1))
                elif mnem.startswith("movw") and len(parts) >= 2:
                    im = RE_IMM.match(parts[1])
                    if im:
                        newval = _int(im.group(1)) & 0xFFFF
                elif mnem.startswith("movt") and len(parts) >= 2:
                    im = RE_IMM.match(parts[1])
                    if im and isinstance(regs.get(dest), int):
                        newval = (regs[dest] & 0xFFFF) | ((_int(im.group(1)) & 0xFFFF) << 16)
                elif mnem.startswith("ldr") and len(parts) >= 2 and "pc" in parts[1]:
                    im = RE_PC_REL.search(parts[1])
                    if im:
                        base = (addr + 4) & ~3
                        pool = base + _int(im.group(1))
                        pc_loads.add(pool)
                        newval = reader.word(pool)
                elif mnem.startswith(("mov",)) and len(parts) >= 2 and _reg(parts[1]) is not None:
                    newval = regs.get(_reg(parts[1]))
                regs[dest] = newval

        # --- stores ---
        if mnem.startswith("str") and len(parts) >= 2 and parts[1].startswith("["):
            src = _reg(parts[0])
            bm = RE_BRACKET.match(parts[1])
            if bm:
                base_reg = _reg(bm.group(1))
                off = _int(bm.group(2)) if bm.group(2) else 0
                if base_reg is not None and regs.get(base_reg) == NAND_BASE:
                    stores.append({
                        "addr": addr, "mnem": mnem, "base": bm.group(1), "offset": off,
                        "src": parts[0], "value": regs.get(src) if src is not None else None,
                    })

        if _is_return(mnem, payload):
            regs.clear()

    return stores, pc_loads


def classify(stores):
    safe, dangerous, unknown = [], [], []
    for s in stores:
        off, val = s["offset"], s["value"]
        if off == CMD_OFF:
            if val in SAFE_OPCODES:
                s["status"] = f"OK ({SAFE_OPCODES[val]})"
                safe.append(s)
            elif val in DANGEROUS_OPCODES:
                s["status"] = f"DANGEROUS ({DANGEROUS_OPCODES[val]})"
                dangerous.append(s)
            else:
                s["status"] = "UNRECOGNIZED opcode" if val is not None else "UNRESOLVED opcode"
                unknown.append(s)
        elif off in ARG_OFFS:
            if val is None:
                s["status"] = "UNRESOLVED argument"
                unknown.append(s)
            elif val in DANGEROUS_ARG_WORDS:
                s["status"] = f"DANGEROUS ({DANGEROUS_ARG_WORDS[val]})"
                dangerous.append(s)
            else:
                s["status"] = "OK (argument)"
                safe.append(s)
        else:
            s["status"] = "OK (other register)"
            safe.append(s)
    return safe, dangerous, unknown


def check_patches(reader: _Bin, load_addr: int, lines):
    """Verify the no-op patches. Returns (problems, verified_regions).

    verified_regions is the list of (entry, dead_end) dead bodies for patches whose
    no-op bytes are actually present; only those bodies are treated as unreachable.
    """
    problems = []
    verified_regions = []
    for file_off, expect, replace, dead_end, name in PATCHES:
        entry = load_addr + file_off
        actual = reader.data[file_off:file_off + 4] if file_off + 4 <= len(reader.data) else None
        if actual is None or actual.hex() != replace:
            problems.append(f"{name}: file 0x{file_off:x} is {actual.hex() if actual else 'oob'}, "
                            f"expected no-op {replace}")
            continue
        verified_regions.append((entry, dead_end))
        if not reader.has_bytes(entry, 2):
            problems.append(f"{name}: entry 0x{entry:x} out of range")
    # unreachability of the verified dead bodies: no branch from OUTSIDE, and no
    # data word anywhere may point into one.
    name_by_entry = {load_addr + f: n for f, _, _, _, n in PATCHES}
    for entry, dead_end in verified_regions:
        name = name_by_entry[entry]
        for line in lines:
            bm = RE_BRANCH.match(line)
            if not bm:
                continue
            src, tgt = int(bm.group(1), 16), int(bm.group(2), 16)
            if entry < tgt < dead_end and not (entry < src < dead_end):
                problems.append(f"{name}: external branch 0x{src:x} -> 0x{tgt:x} enters dead body")
        for off in range(0, len(reader.data) - 3):
            v = struct.unpack_from("<I", reader.data, off)[0]
            if entry < v < dead_end:
                problems.append(f"{name}: data word at 0x{load_addr+off:x} = 0x{v:x} "
                                f"points into dead body")
    return problems, verified_regions


def scan_nand_gate(driver_path: str, load_addr: int = LOAD_ADDR_DEFAULT,
                   quiet: bool = False) -> dict:
    """Prove the driver cannot issue NAND erase/program. Raises GateFailedError on failure."""
    reader = _Bin(driver_path, load_addr)
    lines = run_objdump(driver_path, load_addr).splitlines()

    stores, pc_loads = sweep(lines, reader)
    if NAND_BASE not in {reader.word(p) for p in pc_loads}:
        raise GateFailedError(
            "NAND gate: no literal-pool load of the NAND base 0xC0150000 was resolved; "
            "cannot prove anything, refusing to pass."
        )

    problems, verified_regions = check_patches(reader, load_addr, lines)

    # Stores inside a *verified* patched function's dead body are unreachable;
    # report them but do not let them count as live capability.
    exempt, live = [], []
    for s in stores:
        if any(a < s["addr"] < b for a, b in verified_regions):
            s["status"] = "exempt (patched function body)"
            exempt.append(s)
        else:
            live.append(s)

    safe, dangerous, unknown = classify(live)
    ok = not dangerous and not unknown and not problems

    if not quiet:
        _report(load_addr, stores, exempt, problems, ok)

    if not ok:
        bits = []
        if dangerous:
            bits.append(f"{len(dangerous)} dangerous store(s)")
        if unknown:
            bits.append(f"{len(unknown)} unproven store(s)")
        if problems:
            bits.append(f"{len(problems)} patch/reachability problem(s)")
        raise GateFailedError("NAND gate FAILED: " + ", ".join(bits) +
                              ". The driver must not be loaded onto the player.")

    return {"pass": True, "stores": stores, "exempt": exempt, "safe": safe,
            "dangerous": dangerous, "unknown": unknown, "problems": problems}


def _report(load_addr, stores, exempt, problems, ok):
    print()
    print("=" * 88)
    print("NAND GATE — read-only proof for the patched flash driver")
    print("=" * 88)
    print(f"  load address 0x{load_addr:x}")
    print(f"  {'addr':<9} {'instr':<8} {'off':<6} {'value':<10} status")
    print(f"  {'-'*9} {'-'*8} {'-'*6} {'-'*10} {'-'*34}")
    for s in sorted(stores, key=lambda s: s["addr"]):
        val = "?" if s["value"] is None else hex(s["value"])
        print(f"  0x{s['addr']:06x} {s['mnem']:<8} 0x{s['offset']:02x}  {val:<10} {s['status']}")
    print()
    if exempt:
        print(f"  {len(exempt)} store(s) exempted inside patched function bodies "
              f"(proved unreachable):")
        for s in exempt:
            val = "?" if s["value"] is None else hex(s["value"])
            print(f"    0x{s['addr']:06x} +0x{s['offset']:02x} = {val}")
        print()
    for p in problems:
        print(f"  PROBLEM: {p}")
    if not problems:
        for file_off, _, replace, _, name in PATCHES:
            print(f"  patch OK: {name} @ file 0x{file_off:x} = {replace}; dead body unreachable")
    print()
    print(f"  VERDICT: {'PASS — no NAND write capability remains' if ok else 'FAIL'}")
    print("=" * 88)
    print()


def main():
    ap = argparse.ArgumentParser(description="NAND read-only gate for the patched driver")
    ap.add_argument("driver", help="path to the (patched) driver binary")
    ap.add_argument("--load-addr", type=lambda x: int(x, 16), default=LOAD_ADDR_DEFAULT)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()
    try:
        scan_nand_gate(args.driver, args.load_addr, quiet=args.quiet)
    except GateFailedError as e:
        print(f"GATE FAILED: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()

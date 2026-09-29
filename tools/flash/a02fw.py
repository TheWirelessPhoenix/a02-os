#!/usr/bin/env python3
"""A02 firmware image library: build and check UPGRADE.HEX files for the stock on-device updater.

How the stock firmware updates itself (reversed from fwupdate.AP + FWDec.al, 2026-09-28):
  Tools > Firmware upgrade  -> tools.ap finds UPGRADE.HEX in the root of the internal disk
  -> fwupdate.AP decrypts it (FWDec.al), finds FWIMAGE.FW + FLASH_ID.BIN, checks the NAND ID,
     writes FWIMAGE.FW (the LFI) to the firmware area, then reads everything back and checks
     the LFI header checksum and every file checksum before it reports success.

UPGRADE.HEX layout (Actions FWU v3, "ATJ2127" sector cipher):
  file 0x0000  sector 0 (magic, header, ECIES session-key envelope)       -- taken verbatim
  then plaintext sectors, with the 1 KB block A inserted after plaintext 512*(1+A) and the
  512-byte block B after a further 512*(1+B) (A = byte 0x1ee & 15, B = byte 0x1fe & 15).
  From plaintext offset 0x4000 on, file offset = plaintext offset + 0x800.
  Plaintext = AFI archive {FWIMAGE.FW, FLASH_ID.BIN}; each 512-byte sector is encrypted
  independently (so the updater can seek), first `rounds` 32-byte chunks only, where
  rounds = max(1, 16 - (file_size >> 25)).

We reuse sector 0 / block A / block B of the official A02 .fw, so the player derives the same
session key it already accepts. The key is recovered with `fwkey` (Rockbox atjboottool code).
"""
import hashlib
import sqlite3
import struct
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent

FWU_SIG = bytes.fromhex("112233445566778899aabbccddeeff75")
FWU_SIG2 = bytes.fromhex("3fadf8b02eaf6749b9855f634e5e8e2e")
ATJ2127_KEY = [0x42146EA2, 0x892C8E85, 0x9F9F6D27, 0x545FEDC3,
               0x09E5C0CA, 0x2DFA7E61, 0x4E5322E6, 0xB19185B9]

LFI_MAGIC = 0x0FF0AA55
LFI_DIR_MAX = (0x2000 - 0x200) // 0x20          # 240 entries
# Header bytes the updater replaces with the values already on the device (fwupdate FUN_1c011870).
LFI_PRESERVED = [(0x28, 0x38), (0x4F, 0x50), (0x50, 0x80)]

# Official A02 firmware 1.101.37 (2024-10-19): what is on the device today.
STOCK_FW_SHA = "45b198455d310c15030506c1cefb654e65e505298d0cfa167fdfe1d550586484"
STOCK_AFI_SHA = "dd29194287946d6bf8c8e7ea127d990fd8616b0ab1f5c0925898b50facd9cbf1"
STOCK_LFI_DIR_SUM = 0x3F85B825                   # read from the player's NAND, 2026-09-28
STOCK_LFI_SIZE = 0x383F800
NAND_ID = bytes.fromhex("8984643ca5")            # this unit's NAND (Intel 29F32B08-class)


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def sum16(b):
    return sum(struct.unpack_from("<%dH" % (len(b) // 2), b)) & 0xFFFF


def sum32(b):
    return sum(struct.unpack_from("<%dI" % (len(b) // 4), b)) & 0xFFFFFFFF


def name83(fn):
    base, _, ext = fn.upper().rpartition(".")
    if not base:
        base, ext = ext, ""
    if len(base) > 8 or len(ext) > 3:
        raise ValueError("not an 8.3 name: %r" % fn)
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def pad512(b, fill=b"\0"):
    return b + fill * (-len(b) % 512)


# ---------------------------------------------------------------- firmware database (.afi)

class FirmwareDB:
    """The decrypted official .fw is an SQLite database; FileTable holds every file."""

    def __init__(self, path):
        self.db = sqlite3.connect("file:%s?mode=ro" % path, uri=True)

    def value(self, table):
        row = self.db.execute("select * from %s" % table).fetchone()
        return None if row is None else row[0]

    def files(self, keyword):
        return [(name, bytes(blob)) for name, blob in self.db.execute(
            "select FileName, File from FileTable where Keyword=? order by rowid", (keyword,))]


# ---------------------------------------------------------------- LFI (FWIMAGE.FW)

def build_lfi(db, replace=None, add=None):
    """Build the LFI exactly like the vendor Maker: FWIM files in table order, zero-padded to
    512-byte sectors, packed from sector 16. `replace` maps 8.3 file name -> new bytes;
    `add` is a list of (name, bytes) appended at the end."""
    replace = {k.upper(): v for k, v in (replace or {}).items()}
    files = []
    for name, blob in db.files("FWIM"):
        files.append((name, replace.pop(name.upper(), blob)))
    if replace:
        raise ValueError("replace: not in the firmware: %s" % ", ".join(replace))
    files += list(add or [])
    if len(files) > LFI_DIR_MAX:
        raise ValueError("too many files for the LFI directory")

    hdr = bytearray(0x2000)
    body = bytearray()
    sector = 0x2000 // 512
    seen = set()
    for i, (name, blob) in enumerate(files):
        n = name83(name)
        if n in seen:
            raise ValueError("duplicate file %s" % name)
        seen.add(n)
        data = pad512(blob)
        e = 0x200 + i * 0x20
        hdr[e:e + 11] = n
        struct.pack_into("<II", hdr, e + 0x10, sector, len(data))
        struct.pack_into("<I", hdr, e + 0x1C, sum32(data))
        body += data
        sector += len(data) // 512

    struct.pack_into("<I", hdr, 0, LFI_MAGIC)
    version = (db.value("SDK_VER") + db.value("VER")).encode("ascii")   # "1.10"+"1.37"
    hdr[4:4 + len(version)] = version
    struct.pack_into("<I", hdr, 0x10, sum32(bytes(hdr[0x200:0x2000])))
    hdr[0x4F] = db.value("INF_UDISK_SN_SP") or 0
    uid = (db.value("INF_USERDEFINED_ID_48") or "").encode("ascii")
    hdr[0x50:0x50 + len(uid)] = uid
    desc = (db.value("SDK_DESCRIPTION") or "").encode("ascii")
    hdr[0x80:0x80 + len(desc)] = desc
    struct.pack_into("<H", hdr, 0x1FE, sum16(bytes(hdr[:0x1FE])))
    return bytes(hdr) + bytes(body)


def parse_lfi(lfi):
    """Check an LFI the way the updater's verify pass does; return its directory."""
    if struct.unpack_from("<I", lfi, 0)[0] != LFI_MAGIC:
        raise ValueError("LFI: bad magic")
    if sum16(lfi[:0x1FE]) != struct.unpack_from("<H", lfi, 0x1FE)[0]:
        raise ValueError("LFI: bad header checksum")
    if sum32(lfi[0x200:0x2000]) != struct.unpack_from("<I", lfi, 0x10)[0]:
        raise ValueError("LFI: bad directory checksum")
    out = []
    for e in range(0x200, 0x2000, 0x20):
        if lfi[e] == 0:
            break
        sec, length = struct.unpack_from("<II", lfi, e + 0x10)
        chk = struct.unpack_from("<I", lfi, e + 0x1C)[0]
        data = lfi[sec * 512: sec * 512 + (length >> 9) * 512]   # updater sums whole sectors
        if sec * 512 + length > len(lfi):
            raise ValueError("LFI: %s outside the image" % lfi[e:e + 11])
        if sum32(data) != chk:
            raise ValueError("LFI: bad checksum for %s" % lfi[e:e + 11])
        out.append((lfi[e:e + 11].decode("ascii"), sec, length, chk))
    return out


# ---------------------------------------------------------------- AFI container

AFI_ENTRIES = 126


def build_afi(files):
    """AFI archive (Actions/Rockbox layout): 0x20 header, 126 x 0x20 entries, 0x20 post header,
    data 512-aligned. The updater only looks for the 8.3 names and +0x10 offset / +0x14 size."""
    head = bytearray(0x1000)
    data = bytearray()
    off = len(head)
    for i, (name, blob) in enumerate(files):
        e = 0x20 + i * 0x20
        head[e:e + 11] = name83(name)
        head[e + 11] = ord("I")
        struct.pack_into("<III", head, e + 0x0C, 0, off, len(blob))
        struct.pack_into("<I", head, e + 0x1C, afi_checksum(blob))
        chunk = pad512(blob)
        data += chunk
        off += len(chunk)
    head[0:4] = b"AFI\0"
    struct.pack_into("<I", head, 0x10, len(head) + len(data))
    struct.pack_into("<I", head, 0x1000 - 4, afi_checksum(bytes(head[:0x1000 - 4])))
    return bytes(head) + bytes(data)


def afi_checksum(b):
    n = len(b) & ~3
    s = sum32(b[:n])
    rest = b[n:]
    if len(rest) == 1:
        s += rest[0]
    elif len(rest) == 2:
        s += struct.unpack("<H", rest)[0]
    elif len(rest) == 3:
        s += struct.unpack("<H", rest[:2])[0] + (rest[2] << 16)
    return s & 0xFFFFFFFF


# ---------------------------------------------------------------- FWU v3 envelope + cipher

def session_key(fw_path):
    """(key 32 bytes, blockA, blockB) of an FWU v3 file, via the fwkey helper."""
    tool = HERE / "fwkey"
    if not tool.exists():
        subprocess.run(["make", "-C", str(HERE), "fwkey"], check=True, capture_output=True)
    out = subprocess.run([str(tool), str(fw_path)], check=True, capture_output=True, text=True)
    k, a, b = out.stdout.split()
    return bytes.fromhex(k), int(a), int(b)


def _sector_crypt(sector, key, rounds, encrypt):
    w = list(struct.unpack("<128I", sector))
    s = struct.unpack("<8I", key)
    k = [ATJ2127_KEY[i] ^ s[i] for i in range(8)]
    for r in range(rounds):
        b = w[r * 8:(r + 1) * 8]
        if encrypt:
            c = [b[i] ^ k[i + 1] for i in range(7)] + [b[7] ^ k[1] ^ k[4]]
        else:
            c = b
            b = [c[i] ^ k[i + 1] for i in range(7)] + [c[7] ^ k[1] ^ k[4]]
        k = [k[0]] + k[2:8] + [c[7] ^ s[7]]
        w[r * 8:(r + 1) * 8] = c if encrypt else b
    return struct.pack("<128I", *w)


def crypt(plain, key, rounds, encrypt):
    assert len(plain) % 512 == 0
    return b"".join(_sector_crypt(plain[i:i + 512], key, rounds, encrypt)
                    for i in range(0, len(plain), 512))


def rounds_for(file_size):
    return max(1, 16 - (file_size >> 25))


def envelope(official):
    """Sector 0, block A and block B of an FWU v3 file, plus the A/B selectors."""
    a, b = official[0x1EE] & 15, official[0x1FE] & 15
    offa, offb = 512 * (1 + a), 512 * (1 + b)
    sec0 = official[:512]
    blk_a = official[512 + offa: 512 + offa + 1024]
    blk_b = official[512 + offa + 1024 + offb: 512 + offa + 1024 + offb + 512]
    return sec0, blk_a, blk_b, offa, offb


def wrap(plain, official, key):
    """Encrypt `plain` (AFI) and interleave it with the official envelope -> UPGRADE.HEX bytes."""
    plain = pad512(plain)
    sec0, blk_a, blk_b, offa, offb = envelope(official)
    size = len(plain) + 0x800
    ct = crypt(plain, key, rounds_for(size), True)
    out = bytearray(sec0) + ct[:offa] + blk_a + ct[offa:offa + offb] + blk_b + ct[offa + offb:]
    struct.pack_into("<I", out, 0x10, len(out))        # fw_size (not covered by any check)
    assert len(out) == size
    return bytes(out)


def unwrap(hexfile, key):
    """Inverse of wrap(), for verification: returns the decrypted AFI."""
    a, b = hexfile[0x1EE] & 15, hexfile[0x1FE] & 15
    offa, offb = 512 * (1 + a), 512 * (1 + b)
    ct = (hexfile[512:512 + offa] + hexfile[512 + offa + 1024: 512 + offa + 1024 + offb]
          + hexfile[512 + offa + 1024 + offb + 512:])
    return crypt(ct, key, rounds_for(len(hexfile)), False)

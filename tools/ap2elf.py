#!/usr/bin/env python3
"""Convert Actions US215A 'yqhx' application files (*.ap) to ARM ELF for Ghidra.

Header is the same as *.drv (see yqhx2elf.py) for text/data/bss/entry. Banked code is
described by tables in each 0x800-byte block from file offset 0x800 to 0x2800,
of 12-byte entries {file_offset, vaddr, size} (zero entries are unused slots).
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from yqhx2elf import parse, write_elf  # noqa: E402


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: ap2elf.py in.ap out.elf')
    d = open(sys.argv[1], 'rb').read()
    f = parse(d)
    segs = [('.text', f['text_addr'], d[f['text_off']:f['text_off'] + f['text_len']], 5),
            ('.data', f['data_addr'], d[f['data_off']:f['data_off'] + f['data_len']], 6)]
    if f['bss_len']:
        segs.append(('.bss', f['bss_addr'], f['bss_len'], 6))
    for base in range(0x800, 0x2800, 0x800):
        for p in range(base, base + 0x800 - 11, 12):
            off, addr, size = struct.unpack_from('<3I', d, p)
            if off and size:
                segs.append(('.b%04x' % p, addr, d[off:off + size], 5))
    write_elf(sys.argv[2], segs, f['init'])
    print('%s: %d banks' % (sys.argv[1], len(segs) - 3))


if __name__ == '__main__':
    main()

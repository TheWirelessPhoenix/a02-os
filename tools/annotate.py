#!/usr/bin/env python3
"""Annotate a Ghidra decompile of a yqhx driver: replace DAT_xxxxxxxx pointer loads with their values.

usage: annotate.py <driver.drv> <decompiled.c> <out.c>
(Produce <decompiled.c> with tools/ghidra/DumpDecomp.java on the ELF from yqhx2elf.py.)
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from yqhx2elf import parse, segments  # noqa: E402


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    d = open(sys.argv[1], 'rb').read()
    segs = segments(d, parse(d))

    def rd(a):
        for _, s, b, _ in segs:
            if not isinstance(b, int) and s <= a < s + len(b) - 3:
                return struct.unpack_from('<I', b, a - s)[0]
        return None

    src = open(sys.argv[2]).read()

    def sub(m):
        v = rd(int(m.group(1), 16))
        return m.group(0) if v is None else '(0x%08x)' % v

    open(sys.argv[3], 'w').write(re.sub(r'DAT_([0-9a-f]{8})', sub, src))
    print('wrote', sys.argv[3])


if __name__ == '__main__':
    main()

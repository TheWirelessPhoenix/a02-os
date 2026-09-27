#!/usr/bin/env python3
"""Convert Actions US215A (ATJ2157) 'yqhx' driver files (*.drv) to ARM ELF for Ghidra.

Header layout (drv_head_t as used by Actions' US21x SDK family):
  +0  file_type, drv_type(group), major, minor
  +4  magic 'yqhx'
  +8  text_offset, text_length, text_addr
  +14 data_offset, data_length, data_addr
  +20 bss_length, bss_addr
  +28 init_entry, exit_entry
  +30 bank_a_off, bank_b_off, bank_c_off
  +3c op_entry

Banked-code virtual address (derived empirically on US215A, see re/FINDINGS.md):
  addr = (kind << 27) | (group << 23) | (index << 17) | (window_low + offset)
  bank A region -> kind 2, 0x400 per bank, window_low 0x000
  bank B region -> kind 1, 0x800 per bank, window_low 0x400
  bank C region -> kind 3, size/window UNVERIFIED (0x400 / 0x000 assumed)
"""
import struct
import sys

BANKS = (  # (header slot, kind, bank size, window low)
    (0, 2, 0x400, 0x000),
    (1, 1, 0x800, 0x400),
    (2, 3, 0x400, 0x000),
)


def parse(d):
    h = struct.unpack_from('<BBBB4s14I', d)
    if h[4] != b'yqhx':
        raise ValueError('not a yqhx file')
    f = dict(zip(('text_off', 'text_len', 'text_addr', 'data_off', 'data_len', 'data_addr',
                  'bss_len', 'bss_addr', 'init', 'exit', 'bank_a', 'bank_b', 'bank_c', 'op'), h[5:]))
    f['type'], f['group'] = h[0], h[1]
    return f


def segments(d, f):
    segs = []
    if f['text_len']:
        segs.append(('.text', f['text_addr'], d[f['text_off']:f['text_off'] + f['text_len']], 5))
    if f['data_len']:
        segs.append(('.data', f['data_addr'], d[f['data_off']:f['data_off'] + f['data_len']], 6))
    if f['bss_len'] and f['bss_addr']:
        segs.append(('.bss', f['bss_addr'], f['bss_len'], 6))
    offs = [f['bank_a'], f['bank_b'], f['bank_c']]
    for slot, kind, size, low in BANKS:
        start = offs[slot]
        if not start:
            continue
        later = [o for o in offs + [len(d)] if o > start]
        end = min(later)
        for i in range((end - start) // size):
            addr = (kind << 27) | (f['group'] << 23) | (i << 17) | low
            name = '.bank%s%02d' % ('ABC'[slot], i)
            segs.append((name, addr, d[start + i * size:start + (i + 1) * size], 5))
    return segs


def write_elf(path, segs, entry):
    shstr = b'\0' + b''.join(s[0].encode() + b'\0' for s in segs) + b'.shstrtab\0'
    nseg = len(segs)
    ehsize, phsize, shsize = 52, 32, 40
    off = ehsize + phsize * nseg
    body = b''
    layout = []
    for name, addr, blob, flags in segs:
        if isinstance(blob, int):  # bss
            layout.append((off + len(body), 0, blob))
        else:
            layout.append((off + len(body), len(blob), len(blob)))
            body += blob
            body += b'\0' * (-len(body) % 4)
    shstr_off = off + len(body)
    body += shstr
    body += b'\0' * (-len(body) % 4)
    shoff = off + len(body)

    out = bytearray()
    out += b'\x7fELF\x01\x01\x01' + b'\0' * 9
    out += struct.pack('<HHIIIIIHHHHHH', 2, 40, 1, entry, ehsize, shoff, 0x05000400,
                       ehsize, phsize, nseg, shsize, nseg + 2, nseg + 1)
    for (name, addr, blob, flags), (fo, fsz, msz) in zip(segs, layout):
        out += struct.pack('<IIIIIIII', 1, fo, addr, addr, fsz, msz, flags, 4)
    out += body
    out += b'\0' * shsize
    npos = 1
    for (name, addr, blob, flags), (fo, fsz, msz) in zip(segs, layout):
        stype = 8 if isinstance(blob, int) else 1
        shflags = 2 | (4 if flags & 1 else 1)
        out += struct.pack('<IIIIIIIIII', npos, stype, shflags, addr, fo, msz, 0, 0, 4, 0)
        npos += len(name) + 1
    out += struct.pack('<IIIIIIIIII', npos, 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0)
    open(path, 'wb').write(out)


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: yqhx2elf.py in.drv out.elf')
    d = open(sys.argv[1], 'rb').read()
    f = parse(d)
    segs = segments(d, f)
    write_elf(sys.argv[2], segs, f['init'] or f['op'])
    print('%s group=%d init=%#x exit=%#x op=%#x' % (sys.argv[1], f['group'], f['init'], f['exit'], f['op']))
    for name, addr, blob, _ in segs:
        print('  %-9s %#010x %#x' % (name, addr, blob if isinstance(blob, int) else len(blob)))


if __name__ == '__main__':
    main()

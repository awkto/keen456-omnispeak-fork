#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Unpack an LZEXE 0.90 / 0.91 compressed MS-DOS executable.

Port of the public UNLZEXE algorithm (Mitugu Kurizono, 1990). Produces a plain MZ
executable: decompressed load image, rebuilt relocation table, restored CS:IP / SS:SP.

    oracle/tools/unlzexe.py KEEN4E.EXE out.exe
    oracle/tools/unlzexe.py --info KEEN4E.EXE        # just report what it is
"""
import os
import struct
import sys


class NotLZEXE(ValueError):
    pass


def _words(buf, off, n):
    return list(struct.unpack_from("<%dH" % n, buf, off))


def unlzexe(data: bytes) -> bytes:
    if data[:2] not in (b"MZ", b"ZM"):
        raise NotLZEXE("not an MZ executable")
    ih = _words(data, 0, 0x0E)
    sig = data[0x1C:0x20]
    if ih[0x0C] != 0x1C or sig not in (b"LZ09", b"LZ91"):
        raise NotLZEXE("no LZ09/LZ91 signature at 0x1C")
    ver = 90 if sig == b"LZ09" else 91

    # 8-word info block at the start of the stub's code segment
    stub = (ih[0x0B] + ih[4]) << 4
    inf = _words(data, stub, 8)
    oh = ih[:]
    oh[0x0A], oh[0x0B], oh[0x08], oh[0x07] = inf[0], inf[1], inf[2], inf[3]
    oh[0x0C] = 0x1C

    # relocation table, stored compressed inside the stub
    relocs = []
    if ver == 90:
        p = stub + 0x19D
        for seg in range(0, 0x10000, 0x1000):
            (count,) = struct.unpack_from("<H", data, p); p += 2
            for _ in range(count):
                (off,) = struct.unpack_from("<H", data, p); p += 2
                relocs.append((off, seg))
    else:
        p = stub + 0x158
        off = seg = 0
        while True:
            span = data[p]; p += 1
            if span == 0:
                (span,) = struct.unpack_from("<H", data, p); p += 2
                if span == 0:
                    seg = (seg + 0x0FFF) & 0xFFFF
                    continue
                if span == 1:
                    break
            off += span
            seg = (seg + ((off & ~0x0F) >> 4)) & 0xFFFF
            off &= 0x0F
            relocs.append((off, seg))
    oh[3] = len(relocs)
    hdr_len = (0x1C + 4 * len(relocs) + 0x1FF) & ~0x1FF
    oh[4] = hdr_len >> 4

    # LZ77 stream: starts inf[4] paragraphs below the stub
    src = ((ih[0x0B] - inf[4] + ih[4]) << 4)
    out = bytearray()
    pos = src
    bitbuf = data[pos] | (data[pos + 1] << 8); pos += 2
    bitcnt = 16

    def getbit():
        nonlocal bitbuf, bitcnt, pos
        b = bitbuf & 1
        bitcnt -= 1
        if bitcnt == 0:
            bitbuf = data[pos] | (data[pos + 1] << 8); pos += 2
            bitcnt = 16
        else:
            bitbuf >>= 1
        return b

    while True:
        if getbit():
            out.append(data[pos]); pos += 1
            continue
        if not getbit():
            length = getbit() << 1
            length |= getbit()
            length += 2
            span = data[pos] - 0x100; pos += 1
        else:
            lo = data[pos]; hi = data[pos + 1]; pos += 2
            span = (lo | ((hi & ~0x07) << 5) | 0xE000) - 0x10000
            length = (hi & 0x07) + 2
            if length == 2:
                length = data[pos]; pos += 1
                if length == 0:
                    break           # end of stream
                if length == 1:
                    continue        # segment change marker
                length += 1
        start = len(out) + span
        if start < 0:
            raise ValueError("corrupt LZEXE stream (back-reference before start)")
        for i in range(length):
            out.append(out[start + i])

    # fix up header sizes
    total = hdr_len + len(out)
    oh[1] = total & 0x1FF
    oh[2] = (total + 0x1FF) >> 9
    oh[5] = (oh[5] - (inf[5] + ((inf[6] + 15) >> 4) + 9)) & 0xFFFF
    if oh[6] != 0xFFFF:
        oh[6] = (oh[6] - (ih[5] - oh[5])) & 0xFFFF

    header = bytearray(hdr_len)
    struct.pack_into("<14H", header, 0, *oh)
    for i, (o, s) in enumerate(relocs):
        struct.pack_into("<HH", header, 0x1C + 4 * i, o, s)
    return bytes(header) + bytes(out)


def main(argv):
    if len(argv) >= 2 and argv[1] == "--info":
        for path in argv[2:]:
            if not os.path.isfile(path):
                print(f"SKIP: input not found: {path} (game data absent? see docs/DATA.md)")
                continue
            d = open(path, "rb").read()
            try:
                u = unlzexe(d)
                print(f"{path}: LZEXE {d[0x1C:0x20].decode()} packed={len(d)} unpacked={len(u)} relocs={struct.unpack_from('<H', u, 6)[0]}")
            except NotLZEXE as e:
                print(f"{path}: {e}")
        return 0
    if len(argv) != 3:
        print(__doc__)
        return 2
    if not os.path.isfile(argv[1]):
        print(f"SKIP: input not found: {argv[1]} (game data absent? see docs/DATA.md)")
        return 0
    open(argv[2], "wb").write(unlzexe(open(argv[1], "rb").read()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

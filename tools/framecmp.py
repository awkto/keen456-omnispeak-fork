#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# framecmp.py: compare two OMNI_DEBUG_FRAMES dumps (see docs/INTERP.md).
#
# Each file is a sequence of 320x200 frames, one byte per pixel (EGA palette
# index 0-15, row-major), 64000 bytes per frame. A is the vanilla renderer, B
# is the interpolated renderer in grid mode; the two runs may be offset by a
# frame or so at the start, which is handled by a shift search.

import argparse
import os
import struct
import sys
import zlib

WIDTH = 320
HEIGHT = 200
FRAME_SIZE = WIDTH * HEIGHT

# Standard 16-colour EGA palette (index 0-15 -> RGB).
EGA_PALETTE = [
    (0x00, 0x00, 0x00),  # 0  black
    (0x00, 0x00, 0xAA),  # 1  blue
    (0x00, 0xAA, 0x00),  # 2  green
    (0x00, 0xAA, 0xAA),  # 3  cyan
    (0xAA, 0x00, 0x00),  # 4  red
    (0xAA, 0x00, 0xAA),  # 5  magenta
    (0xAA, 0x55, 0x00),  # 6  brown
    (0xAA, 0xAA, 0xAA),  # 7  light grey
    (0x55, 0x55, 0x55),  # 8  dark grey
    (0x55, 0x55, 0xFF),  # 9  light blue
    (0x55, 0xFF, 0x55),  # 10 light green
    (0x55, 0xFF, 0xFF),  # 11 light cyan
    (0xFF, 0x55, 0x55),  # 12 light red
    (0xFF, 0x55, 0xFF),  # 13 light magenta
    (0xFF, 0xFF, 0x55),  # 14 yellow
    (0xFF, 0xFF, 0xFF),  # 15 white
]
EGA_PALETTE_BYTES = [bytes(c) for c in EGA_PALETTE]


def load_frames(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) % FRAME_SIZE != 0:
        raise ValueError(
            "%s: size %d is not a multiple of %d bytes"
            % (path, len(data), FRAME_SIZE)
        )
    return data


def frame(data, i):
    return data[i * FRAME_SIZE:(i + 1) * FRAME_SIZE]


def parse_ignore_rows(text):
    if text is None:
        return None
    parts = text.split(":")
    if len(parts) != 2:
        raise ValueError("--ignore-rows expects N0:N1")
    n0, n1 = int(parts[0]), int(parts[1])
    if n0 < 0 or n1 < n0 or n1 > HEIGHT:
        raise ValueError("--ignore-rows out of range (0 <= N0 <= N1 <= %d)" % HEIGHT)
    return (n0, n1)


def row_ignored(y, ignore):
    return ignore is not None and ignore[0] <= y < ignore[1]


def diff_stats(a, b, ignore):
    """Return (count, bbox) for one frame pair; bbox is (x0,y0,x1,y1) inclusive."""
    count = 0
    x0 = y0 = WIDTH
    x1 = y1 = -1
    for y in range(HEIGHT):
        if row_ignored(y, ignore):
            continue
        base = y * WIDTH
        for x in range(WIDTH):
            if a[base + x] != b[base + x]:
                count += 1
                if x < x0:
                    x0 = x
                if x > x1:
                    x1 = x
                if y < y0:
                    y0 = y
                if y > y1:
                    y1 = y
    if count == 0:
        return 0, None
    return count, (x0, y0, x1, y1)


def frames_differ(a, b, ignore):
    """Cheap test: True if any non-ignored pixel differs."""
    if ignore is None:
        return a != b
    if a == b:
        return False
    for y in range(HEIGHT):
        if row_ignored(y, ignore):
            continue
        base = y * WIDTH
        if a[base:base + WIDTH] != b[base:base + WIDTH]:
            return True
    return False


def overlap_range(n_a, n_b, shift):
    """A frame indices i such that i and i+shift are both valid."""
    lo = max(0, -shift)
    hi = min(n_a, n_b - shift)
    if hi <= lo:
        return lo, hi
    return lo, hi


def choose_shift(a_data, b_data, n_a, n_b, max_shift, ignore):
    best = None
    for shift in range(-max_shift, max_shift + 1):
        lo, hi = overlap_range(n_a, n_b, shift)
        if hi <= lo:
            continue
        differing = 0
        for i in range(lo, hi):
            if frames_differ(frame(a_data, i), frame(b_data, i + shift), ignore):
                differing += 1
        overlap = hi - lo
        # Fewest differing frames wins; more overlap breaks ties.
        key = (differing, -overlap, abs(shift), shift)
        if best is None or key < best[0]:
            best = (key, shift)
    return None if best is None else best[1]


def png_chunk(typ, data):
    out = struct.pack(">I", len(data)) + typ + data
    out += struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)
    return out


def write_png(path, rows):
    raw = bytearray()
    for r in rows:
        raw.append(0)  # filter type: none
        raw.extend(r)
    width = WIDTH * 3
    height = len(rows)
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    body = sig
    body += png_chunk(b"IHDR", ihdr)
    body += png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    body += png_chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(body)


def compose_rows(a, b, ignore):
    """Three panels side by side: A, B, and the diff mask."""
    rows = []
    for y in range(HEIGHT):
        base = y * WIDTH
        row = bytearray()
        for x in range(WIDTH):
            row += EGA_PALETTE_BYTES[a[base + x] & 0x0F]
        for x in range(WIDTH):
            row += EGA_PALETTE_BYTES[b[base + x] & 0x0F]
        ign = row_ignored(y, ignore)
        for x in range(WIDTH):
            diff = (not ign) and a[base + x] != b[base + x]
            row += b"\xff\xff\xff" if diff else b"\x00\x00\x00"
        rows.append(bytes(row))
    return rows


def main(argv):
    parser = argparse.ArgumentParser(
        prog="framecmp.py",
        description="Compare two OMNI_DEBUG_FRAMES dumps.",
    )
    parser.add_argument("a", help="vanilla dump (file A)")
    parser.add_argument("b", help="interpolated dump (file B)")
    parser.add_argument("--max-shift", type=int, default=3,
                        help="search shifts in [-N, +N] (default 3)")
    parser.add_argument("--png-dir",
                        help="write PNGs of the first 3 differing frames here")
    parser.add_argument("--ignore-rows",
                        help="ignore differences in rows N0:N1 (half-open)")
    args = parser.parse_args(argv)

    try:
        ignore = parse_ignore_rows(args.ignore_rows)
    except ValueError as e:
        print("framecmp: %s" % e, file=sys.stderr)
        return 2

    try:
        a_data = load_frames(args.a)
        b_data = load_frames(args.b)
    except (OSError, ValueError) as e:
        print("framecmp: %s" % e, file=sys.stderr)
        return 2

    n_a = len(a_data) // FRAME_SIZE
    n_b = len(b_data) // FRAME_SIZE
    print("A: %s (%d frames)" % (args.a, n_a))
    print("B: %s (%d frames)" % (args.b, n_b))

    shift = choose_shift(a_data, b_data, n_a, n_b, args.max_shift, ignore)
    if shift is None:
        print("framecmp: no overlapping range for shifts in [%d, %d]"
              % (-args.max_shift, args.max_shift), file=sys.stderr)
        return 2
    print("chosen shift: %+d" % shift)

    lo, hi = overlap_range(n_a, n_b, shift)
    compared = hi - lo
    differing_frames = []
    for i in range(lo, hi):
        a = frame(a_data, i)
        b = frame(b_data, i + shift)
        count, bbox = diff_stats(a, b, ignore)
        if count:
            differing_frames.append((i, count, bbox))

    identical = compared - len(differing_frames)
    print("frames compared: %d" % compared)
    print("identical: %d" % identical)
    print("differing: %d" % len(differing_frames))
    for i, count, bbox in differing_frames[:10]:
        x0, y0, x1, y1 = bbox
        print("frame %d: %d differing pixels, bbox (%d,%d,%d,%d)"
              % (i, count, x0, y0, x1, y1))

    if args.png_dir:
        try:
            os.makedirs(args.png_dir, exist_ok=True)
        except OSError as e:
            print("framecmp: %s" % e, file=sys.stderr)
            return 2
        for i, _count, _bbox in differing_frames[:3]:
            a = frame(a_data, i)
            b = frame(b_data, i + shift)
            out = os.path.join(args.png_dir, "frame_%05d.png" % i)
            write_png(out, compose_rows(a, b, ignore))
            print("wrote %s" % out)

    return 0 if not differing_frames else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
